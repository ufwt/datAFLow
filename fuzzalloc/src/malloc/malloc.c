//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <errno.h>    // for errno, EINVAL, ENOMEM
#include <stddef.h>   // for ptrdiff_t
#include <stdint.h>   // for uintptr_t
#include <stdlib.h>   // for abort
#include <string.h>   // for memcpy, memset
#include <sys/mman.h> // for mmap
#include <unistd.h>   // for getpagesize

#include "fuzzalloc.h"
#include "malloc_internal.h"

// Forward declarations
void free(void *ptr);

/// Maps malloc/calloc tags (inserted during compilation) to allocation pool
/// IDs
static uint16_t pool_map[TAG_MAX + 1];

// XXX should this be a constant?
static int page_size;

__attribute__((constructor)) static void __init_fuzzalloc(void) {
  page_size = getpagesize();
}

static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

static inline struct chunk_t *find_free_chunk(const struct pool_t *pool,
                                              size_t size) {
  struct chunk_t *chunk = pool->free_list;

  while (chunk && CHUNK_SIZE(chunk) < size) {
    chunk = chunk->next;
  }

  return chunk;
}

static inline void in_use_sanity_check(struct chunk_t *chunk) {
  size_t chunk_size = CHUNK_SIZE(chunk);
  struct chunk_t *next_chunk = NEXT_CHUNK(chunk);
  size_t next_prev_chunk_size = PREV_CHUNK_SIZE(next_chunk);

  if (!CHUNK_IN_USE(chunk)) {
    DEBUG_MSG("chunk %p should be marked in use\n", chunk);
    goto do_abort;
  } else if (!PREV_CHUNK_IN_USE(next_chunk)) {
    DEBUG_MSG("next chunk %p should mark previous chunk %p in use\n",
              next_chunk, chunk);
    goto do_abort;
  } else if (chunk_size != next_prev_chunk_size) {
    DEBUG_MSG(
        "chunk %p size %lu should equal previous chunk %p previous size %lu\n",
        chunk, chunk_size, next_chunk, next_prev_chunk_size);
    goto do_abort;
  }

  return;

do_abort:
  abort();

  if (CHUNK_IN_USE(chunk) && PREV_CHUNK_IN_USE(next_chunk) &&
      (chunk_size == PREV_CHUNK_SIZE(next_chunk))) {
    return;
  } else {
    DEBUG_MSG("chunk in use sanity check failed (chunk = %p)\n", chunk);
    abort();
  }
}

void *__tagged_malloc(tag_t tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%u, %lu)\n", tag, size);

  if (size == 0) {
    return NULL;
  }

  void *mem = NULL;
  size_t chunk_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGNMENT);
  uint16_t pool_id = pool_map[tag];

  if (pool_id == 0) {
    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site

    // If the requested size cannot fit in an allocation pool, return an error
    if (size > POOL_SIZE) {
      DEBUG_MSG("memory request too large for an allocation pool\n");
      errno = EINVAL;
      return NULL;
    }

    // Adjust the allocation size so that it is properly aligned
    size_t pool_size = align(size + POOL_OVERHEAD, POOL_ALIGNMENT);

    // mmap the requested amount of memory
    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", pool_size);
    void *mmap_base = mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmap_base == (void *)(-1)) {
      DEBUG_MSG("mmap failed: %s\n", strerror(errno));
      errno = ENOMEM;

      return NULL;
    }
    DEBUG_MSG("mmap base at %p\n", mmap_base);

    // Adjust the mmap'd memory so that it is aligned with a valid pool ID. If
    // necessary, unmap wasted memory between the original mapping and the
    // aligned mapping. munmap works at a page granularity, meaning all pages
    // containing a part of the indicated range are unmapped. To prevent
    // SIGSEGVs by unmapping a page that is partly used, only unmap at the page
    // level
    void *pool_base = (void *)align((uintptr_t)mmap_base, POOL_ALIGNMENT);
    DEBUG_MSG("allocation pool base at %p\n", pool_base);

    ptrdiff_t adjust = (pool_base - mmap_base) / page_size * page_size;
    if (adjust > 0) {
      munmap(mmap_base, adjust);
      DEBUG_MSG("unmapped %lu bytes before pool base\n", adjust);

      pool_size -= adjust;
    }

    // To ensure alignment with a valid pool ID, we mmap much more memory than
    // we actually require. Adjust the amount of mmap'd memory to the default
    // pool size
    adjust = (pool_size - POOL_SIZE - POOL_OVERHEAD) * page_size / page_size;
    if (adjust > 0) {
      munmap(pool_base + (pool_size - adjust), adjust);
      DEBUG_MSG("unmapped %lu bytes of unused memory\n", adjust);

      pool_size -= adjust;
    }

    // Create an entry chunk that can hold the amount of data requested by the
    // user. Chunks have their own alignment constraints that must satisfy the
    // malloc API
    struct chunk_t *chunk = MEM_TO_CHUNK(
        align((uintptr_t)pool_base + POOL_OVERHEAD + CHUNK_OVERHEAD,
              CHUNK_ALIGNMENT));
    SET_CHUNK_IN_USE_SIZE(chunk, chunk_size);
    SET_PREV_CHUNK_IN_USE(chunk);

    // Create the initial free chunk following the newly-created entry chunk.
    // This chunk will later be insert into the allocation pool's free list
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    SET_PREV_CHUNK_IN_USE_SIZE(free_chunk, chunk_size);
    SET_CHUNK_FREE_SIZE(free_chunk, pool_size - chunk_size);

    free_chunk->prev = free_chunk->next = NULL;

    DEBUG_MSG("chunk created at %p (size %lu bytes)\n", chunk, chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu bytes)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    // Finally, initialise the allocation pool's metadata
    struct pool_t *pool = (void *)pool_base;
    pool->entry = chunk;
    pool->free_list = free_chunk;

    // This is the first memory allocation for this allocation site, so save
    // the allocation pool ID into the pool map
    pool_id = GET_POOL_ID(pool_base);
    DEBUG_MSG("tag %u -> pool ID 0x%x\n", tag, pool_id);
    pool_map[tag] = pool_id;

    mem = CHUNK_TO_MEM(chunk);
  } else {
    // Reuse of an existing allocation site. Try and fit the new memory request
    // into the existing allocation pool

    struct pool_t *pool = GET_POOL(pool_id);

    // Find a suitably-sized free chunk in the allocation pool for this tag
    struct chunk_t *chunk = find_free_chunk(pool, chunk_size);
    if (!chunk) {
      DEBUG_MSG("unable to find a free chunk (min. size %lu bytes) in "
                "allocation pool 0x%x\n",
                chunk_size, pool_id);
      errno = ENOMEM;
      return NULL;
    }
    size_t free_chunk_size = CHUNK_SIZE(chunk);

    // Unlink the chunk from the free list
    SET_CHUNK_IN_USE_SIZE(chunk, chunk_size);
    // TODO fixme
    // chunk->prev->next = chunk->next;
    // chunk->next->prev = chunk->prev;

    // Turn whatever space is left following the newly-allocated chunk into a
    // new free chunk. Insert this free chunk into the allocation pool's free
    // list
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    SET_PREV_CHUNK_IN_USE_SIZE(free_chunk, chunk_size);
    SET_CHUNK_FREE_SIZE(free_chunk, free_chunk_size - chunk_size);

    free_chunk->prev = chunk->prev;
    free_chunk->next = chunk->next;
    if (pool->free_list == chunk) {
      pool->free_list = free_chunk;
    }

    DEBUG_MSG("chunk created at %p (size %lu)\n", chunk, chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    mem = CHUNK_TO_MEM(chunk);
  }

  return mem;
}

void *__tagged_calloc(tag_t tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%u, %lu, %lu)\n", tag, nmemb, size);

  // Adapted from muslc

  if (size && nmemb > (size_t)(-1) / size) {
    errno = ENOMEM;
    return NULL;
  }

  size *= nmemb;
  void *mem = __tagged_malloc(tag, size);
  if (!mem) {
    return mem;
  }

  return memset(mem, 0, size);
}

void *__tagged_realloc(tag_t tag, void *ptr, size_t size) {
  DEBUG_MSG("__tagged_realloc(%u, %p, %lu)\n", tag, ptr, size);

  void *mem = NULL;

  if (!ptr) {
    // Per realloc manual: "if ptr is NULL, then the call is equivalent to
    // malloc(size), for all values of size. This will create a new allocation
    // pool (as this is essentially a new allocation site)
    mem = __tagged_malloc(tag, size);
  } else if (size == 0) {
    // Per realloc manual: "if size is equal to zero, and ptr is not NULL, then
    // the call is equivalent to free(ptr)
    free(ptr);
  } else {
    struct chunk_t *orig_chunk = MEM_TO_CHUNK(ptr);

    // Sanity check that the chunk metadata hasn't been corrupted in some way
    in_use_sanity_check(orig_chunk);

    struct pool_t *pool = GET_POOL(GET_POOL_ID(orig_chunk));

    size_t orig_chunk_size = CHUNK_SIZE(orig_chunk);
    size_t new_chunk_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGNMENT);

    if (orig_chunk_size >= new_chunk_size) {
      // The requested reallocation size is smaller than the existing size. We
      // can just reduce the amount of space allocated for this chunk and add
      // the remaining space as a free chunk

      SET_CHUNK_IN_USE_SIZE(orig_chunk, new_chunk_size);

      // Turn whatever space is left following the newly-resized chunk into a
      // new free chunk. Insert this free chunk into the head of the allocation
      // pool's free list
      struct chunk_t *free_chunk = NEXT_CHUNK(orig_chunk);
      SET_PREV_CHUNK_IN_USE_SIZE(free_chunk, new_chunk_size);
      SET_CHUNK_FREE_SIZE(free_chunk, orig_chunk_size - new_chunk_size);

      free_chunk->prev = pool->free_list->prev;
      free_chunk->next = pool->free_list;
      pool->free_list = free_chunk;

      DEBUG_MSG("existing chunk at %p (size %lu bytes) resized to %lu bytes\n",
                orig_chunk, orig_chunk_size, new_chunk_size);

      mem = CHUNK_TO_MEM(orig_chunk);
    } else {
      // The requested reallocation size is larger than the space currently
      // allocated for this chunk. Find a free chunk in the same allocation
      // pool that will fit the new chunk

      // Find a suitably-sized free chunk in the allocation pool for the
      // reallocation destination
      struct chunk_t *new_chunk = find_free_chunk(pool, new_chunk_size);
      if (!new_chunk) {
        DEBUG_MSG("unable to find a free chunk (min. size %lu bytes) in "
                  "allocation pool 0x%x\n",
                  new_chunk_size, GET_POOL_ID(pool));
        errno = ENOMEM;
        return NULL;
      }
      size_t free_chunk_size = CHUNK_SIZE(new_chunk);

      // Unlink the chunk from the free list
      SET_CHUNK_IN_USE_SIZE(new_chunk, new_chunk_size);
      // TODO fixme
      // chunk->prev->next = chunk->next;
      // chunk->next->prev = chunk->prev;

      // Turn whatever free space is left following the newly-allocated chunk
      // into a new free chunk. Insert this free chunk into the allocation
      // pool's free list
      struct chunk_t *free_chunk = NEXT_CHUNK(new_chunk);
      SET_PREV_CHUNK_IN_USE_SIZE(free_chunk, new_chunk_size);
      SET_CHUNK_FREE_SIZE(free_chunk, free_chunk_size - new_chunk_size);

      free_chunk->prev = new_chunk->prev;
      free_chunk->next = new_chunk->next;
      if (pool->free_list == new_chunk) {
        pool->free_list = free_chunk;
      }

      // Copy the data contained in the original chunk into the new chunk. Free
      // the original chunk
      memcpy(new_chunk + CHUNK_OVERHEAD, orig_chunk + CHUNK_OVERHEAD,
             orig_chunk_size - CHUNK_OVERHEAD);
      free(ptr);

      DEBUG_MSG("chunk moved from %p (size %lu) to %p (size %lu)\n", orig_chunk,
                orig_chunk_size, new_chunk, new_chunk_size);

      mem = CHUNK_TO_MEM(new_chunk);
    }
  }

  return mem;
}

void *malloc(size_t size) { return __tagged_malloc(DEFAULT_TAG, size); }

void *calloc(size_t nmemb, size_t size) {
  return __tagged_calloc(DEFAULT_TAG, nmemb, size);
}

void *realloc(void *ptr, size_t size) {
  return __tagged_realloc(DEFAULT_TAG, ptr, size);
}

void free(void *ptr) {
  DEBUG_MSG("free(%p)\n", ptr);

  if (!ptr) {
    return;
  }

  struct chunk_t *chunk = MEM_TO_CHUNK(ptr);
  size_t chunk_size = CHUNK_SIZE(chunk);
  DEBUG_MSG("freeing memory at %p (size %lu bytes)\n", chunk, chunk_size);

  // Sanity check that the chunk metadata hasn't been corrupted in some way
  in_use_sanity_check(chunk);

  SET_CHUNK_FREE(chunk);

  // TODO coalesce

  // Insert the newly-freed chunk at the head of the allocation pool's free
  // list
  struct chunk_t *free_list = GET_POOL(GET_POOL_ID(chunk))->free_list;
  chunk->prev = free_list->prev;
  chunk->next = free_list;
  free_list = chunk;
}
