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

#include "malloc_internal.h"

// Forward declarations
void free(void *ptr);

/// Maps malloc/calloc/realloc allocation call site tags (inserted during
/// compilation) to allocation pool tags
static tag_t alloc_site_to_pool_map[TAG_MAX + 1];

// XXX should this be a constant?
static int page_size = 0;

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
  }

  if (!PREV_CHUNK_IN_USE(next_chunk)) {
    DEBUG_MSG("next chunk %p should mark previous chunk %p in use\n",
              next_chunk, chunk);
    goto do_abort;
  }

  // XXX check `NEXT_CHUNK(PREV_CHUNK(chunk)) == chunk)` ?

  if (chunk_size != next_prev_chunk_size) {
    DEBUG_MSG(
        "chunk %p size %lu should equal previous chunk %p previous size %lu\n",
        chunk, chunk_size, next_chunk, next_prev_chunk_size);
    goto do_abort;
  }

  return;

do_abort:
  abort();
}

void *__tagged_malloc(tag_t alloc_site_tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(0x%x, %lu) called from %p\n", alloc_site_tag, size,
            __builtin_return_address(0));

  if (size == 0) {
    return NULL;
  }

  void *mem = NULL;
  size_t chunk_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGNMENT);
  tag_t pool_tag = alloc_site_to_pool_map[alloc_site_tag];

  if (pool_tag == 0) {
    // This should only happen once
    if (!page_size) {
      page_size = getpagesize();
    }

    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site
    DEBUG_MSG("creating new allocation pool\n");

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

    // Adjust the mmap'd memory so that it is aligned with a valid pool tag. If
    // necessary, unmap wasted memory between the original mapping and the
    // aligned mapping. munmap works at a page granularity, meaning all pages
    // containing a part of the indicated range are unmapped. To prevent
    // SIGSEGVs by unmapping a page that is partly used, only unmap at the page
    // level
    void *pool_base = (void *)align((uintptr_t)mmap_base, POOL_ALIGNMENT);

    ptrdiff_t adjust = (pool_base - mmap_base) / page_size * page_size;
    if (adjust > 0) {
      munmap(mmap_base, adjust);
      DEBUG_MSG("unmapped %lu bytes before pool base\n", adjust);

      pool_size -= adjust;
    }

    // To ensure alignment with a valid pool tag, we mmap much more memory than
    // we actually require. Adjust the amount of mmap'd memory to the default
    // pool size
    adjust = (pool_size - POOL_SIZE - POOL_OVERHEAD) * page_size / page_size;
    if (adjust > 0) {
      munmap(pool_base + (pool_size - adjust), adjust);
      DEBUG_MSG("unmapped %lu bytes of unused memory\n", adjust);

      pool_size -= adjust;
    }

    DEBUG_MSG("allocation pool base at %p (size %lu bytes)\n", pool_base,
              pool_size);

    // Create an entry chunk that can hold the amount of data requested by the
    // user. Chunks have their own alignment constraints that must satisfy the
    // malloc API
    struct chunk_t *chunk = MEM_TO_CHUNK(
        align((uintptr_t)pool_base + POOL_OVERHEAD + CHUNK_OVERHEAD,
              CHUNK_ALIGNMENT));
    SET_CHUNK_SIZE(chunk, chunk_size);
    SET_CHUNK_IN_USE(chunk);

    // Mark the "previous" chunk of the entry chunk as in use so that we will
    // never try to coalesce it and break something
    SET_PREV_CHUNK_IN_USE(chunk);

    // Create the initial free chunk following the newly-created entry chunk.
    // This chunk will later be insert into the allocation pool's free list
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    // The initial free chunk should not be outside of the allocation pool
    assert((ptrdiff_t)free_chunk < (ptrdiff_t)pool_base + pool_size);
    SET_CHUNK_SIZE(free_chunk, pool_size - chunk_size);
    SET_CHUNK_FREE(free_chunk);
    SET_PREV_CHUNK_IN_USE(free_chunk);

    free_chunk->prev = free_chunk->next = NULL;

    DEBUG_MSG("initial chunk created at %p (size %lu bytes)\n", chunk,
              chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu bytes)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    // Finally, initialise the allocation pool's metadata and insert the free
    // chunk into the pool's free list
    struct pool_t *pool = (void *)pool_base;
    pool->size = pool_size;
    pool->entry = chunk;
    pool->free_list = free_chunk;

    // This is the first memory allocation for this allocation site, so save
    // the allocation pool tag into the pool map (and likewise the allocation
    // site tag into the site map)
    pool_tag = GET_POOL_TAG(pool_base);
    DEBUG_MSG("pool 0x%x (size %lu bytes) created for tag %u\n", pool_tag,
              pool_size, alloc_site_tag);
    alloc_site_to_pool_map[alloc_site_tag] = pool_tag;
    __pool_to_alloc_site_map[pool_tag] = alloc_site_tag;

    mem = CHUNK_TO_MEM(chunk);
  } else {
    // Reuse of an existing allocation site. Try and fit the new memory request
    // into the existing allocation pool
    DEBUG_MSG("reusing allocation pool 0x%x\n", pool_tag);

    struct pool_t *pool = GET_POOL(pool_tag);

    // Find a suitably-sized free chunk in the allocation pool for this tag
    struct chunk_t *chunk = find_free_chunk(pool, chunk_size);
    if (!chunk) {
      DEBUG_MSG("unable to find a free chunk (min. size %lu bytes) in "
                "allocation pool 0x%x\n",
                chunk_size, pool_tag);
      // TODO grow the allocation pool
      abort();
    }
    size_t free_chunk_size = CHUNK_SIZE(chunk);

    // Unlink the chunk from the free list
    if (chunk->prev) {
      chunk->prev->next = chunk->next;
    }
    if (chunk->next) {
      chunk->next->prev = chunk->prev;
    }

    // Mark the chunk as being in use
    SET_CHUNK_SIZE(chunk, chunk_size);
    SET_CHUNK_IN_USE(chunk);

    // Turn whatever space is left following the newly-allocated chunk into a
    // new free chunk. Insert this free chunk into the allocation pool's free
    // list
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    // The new free chunk should not be outside of the allocation pool
    assert((ptrdiff_t)free_chunk < (ptrdiff_t)pool + pool->size);
    SET_CHUNK_SIZE(free_chunk, free_chunk_size - chunk_size);
    SET_CHUNK_FREE(free_chunk);
    SET_PREV_CHUNK_IN_USE(free_chunk);

    free_chunk->prev = chunk->prev;
    free_chunk->next = chunk->next;
    if (pool->free_list == chunk) {
      pool->free_list = free_chunk;
    }

    DEBUG_MSG("chunk created at %p (size %lu bytes)\n", chunk, chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu bytes)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    mem = CHUNK_TO_MEM(chunk);
  }

  return mem;
}

void *__tagged_calloc(tag_t alloc_site_tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(0x%x, %lu, %lu) called from %p\n", alloc_site_tag,
            nmemb, size, __builtin_return_address(0));

  // Adapted from muslc

  if (size && nmemb > (size_t)(-1) / size) {
    errno = ENOMEM;
    return NULL;
  }

  size *= nmemb;
  void *mem = __tagged_malloc(alloc_site_tag, size);
  if (!mem) {
    return mem;
  }

  return memset(mem, 0, size);
}

void *__tagged_realloc(tag_t alloc_site_tag, void *ptr, size_t size) {
  DEBUG_MSG("__tagged_realloc(0x%x, %p, %lu) called from %p\n", alloc_site_tag,
            ptr, size, __builtin_return_address(0));

  void *mem = NULL;

  if (!ptr) {
    // Per realloc manual: "if ptr is NULL, then the call is equivalent to
    // malloc(size), for all values of size. This will create a new allocation
    // pool (as this is essentially a new allocation site)
    mem = __tagged_malloc(alloc_site_tag, size);
  } else if (size == 0) {
    // Per realloc manual: "if size is equal to zero, and ptr is not NULL, then
    // the call is equivalent to free(ptr)
    free(ptr);
  } else {
    // Do a reallocation

    struct chunk_t *orig_chunk = MEM_TO_CHUNK(ptr);

    // Sanity check that the chunk metadata hasn't been corrupted in some way
    in_use_sanity_check(orig_chunk);

    struct pool_t *pool = GET_POOL(GET_POOL_TAG(orig_chunk));

    size_t orig_chunk_size = CHUNK_SIZE(orig_chunk);
    size_t new_chunk_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGNMENT);

    if (orig_chunk_size >= new_chunk_size) {
      // The requested reallocation size is smaller than the existing size. We
      // can just reduce the amount of space allocated for this chunk and add
      // the remaining space as a free chunk

      // Reduce the size of the original chunk. There is no need to adjust the
      // in use bits (this information stays the same)
      SET_CHUNK_SIZE(orig_chunk, new_chunk_size);

      // Turn whatever space is left following the newly-resized chunk into a
      // new free chunk. Insert this free chunk into the head of the allocation
      // pool's free list
      struct chunk_t *free_chunk = NEXT_CHUNK(orig_chunk);
      // Because the allocated memory has shrunk, the remaining memory (and now
      // newly-free chunk) should not be outside of the allocation pool
      assert((ptrdiff_t)free_chunk < (ptrdiff_t)pool + pool->size);
      SET_CHUNK_SIZE(free_chunk, orig_chunk_size - new_chunk_size);
      SET_CHUNK_FREE(free_chunk);
      SET_PREV_CHUNK_IN_USE(free_chunk);

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
                  new_chunk_size, GET_POOL_TAG(pool));
        // TODO grow the allocation pool
        abort();
      }
      size_t free_chunk_size = CHUNK_SIZE(new_chunk);

      // Unlink the chunk from the free list
      if (new_chunk->prev) {
        new_chunk->prev->next = new_chunk->next;
      }
      if (new_chunk->next) {
        new_chunk->next->prev = new_chunk->prev;
      }

      // Mark the chunk as being in use
      SET_CHUNK_SIZE(new_chunk, new_chunk_size);
      SET_CHUNK_IN_USE(new_chunk);

      // Turn whatever free space is left following the newly-allocated chunk
      // into a new free chunk. Insert this free chunk into the allocation
      // pool's free list
      struct chunk_t *free_chunk = NEXT_CHUNK(new_chunk);
      // TODO handle this
      assert((ptrdiff_t)free_chunk < (ptrdiff_t)pool + pool->size);
      SET_CHUNK_SIZE(free_chunk, free_chunk_size - new_chunk_size);
      SET_CHUNK_FREE(free_chunk);
      SET_PREV_CHUNK_IN_USE(free_chunk);

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

      DEBUG_MSG("chunk moved from %p (size %lu bytes) to %p (size %lu bytes)\n",
                orig_chunk, orig_chunk_size, new_chunk, new_chunk_size);

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
  DEBUG_MSG("free(%p) called from %p\n", ptr, __builtin_return_address(0));

  if (!ptr) {
    return;
  }

  struct chunk_t *chunk = MEM_TO_CHUNK(ptr);
  size_t chunk_size = CHUNK_SIZE(chunk);
  DEBUG_MSG("freeing memory at %p (size %lu bytes)\n", chunk, chunk_size);

  // Sanity check that the chunk metadata hasn't been corrupted in some way
  in_use_sanity_check(chunk);

  struct pool_t *pool = GET_POOL(GET_POOL_TAG(chunk));

  struct chunk_t *next_chunk = NEXT_CHUNK(chunk);
  // TODO handle this
  assert((ptrdiff_t)next_chunk < (ptrdiff_t)pool + pool->size);

  // If the previous chunk is free, coalesce
  if (PREV_CHUNK_FREE(chunk)) {
    struct chunk_t *prev_chunk = PREV_CHUNK(chunk);
    DEBUG_MSG(
        "previous chunk at %p (size %lu bytes) is also free - coalescing\n",
        prev_chunk, PREV_CHUNK_SIZE(chunk));

    // Free chunks must always be preceeded by an in use chunk
    assert(PREV_CHUNK_IN_USE(prev_chunk));

    chunk_size += PREV_CHUNK_SIZE(chunk);
    chunk = prev_chunk;
  }

  // If the next chunk is free, coalesce
  if (CHUNK_FREE(next_chunk)) {
    DEBUG_MSG("next chunk at %p (size %lu bytes) is also free - coalescing\n",
              next_chunk, CHUNK_SIZE(next_chunk));

    chunk_size += CHUNK_SIZE(next_chunk);

    next_chunk = NEXT_CHUNK(next_chunk);
    if ((ptrdiff_t)next_chunk >= (ptrdiff_t)pool + pool->size) {
      next_chunk = NULL;
    } else {
      // Free chunks must always be followed by an in use chunk
      assert(CHUNK_IN_USE(next_chunk));
    }
  }

  // Update the size of the current chunk (about to be free)
  SET_CHUNK_SIZE(chunk, chunk_size);
  // Mark the current chunk as free
  SET_CHUNK_FREE(chunk);
  // Mark the next chunk's (if it exists) previous chunk (i.e., the current
  // chunk) as free
  if (next_chunk) {
    SET_PREV_CHUNK_FREE(next_chunk);
  }

  DEBUG_MSG("new freed chunk at %p (size %lu bytes)\n", chunk, chunk_size);

  // Insert the newly-freed chunk at the head of the allocation pool's free
  // list
  struct chunk_t *free_list = GET_POOL(GET_POOL_TAG(chunk))->free_list;
  chunk->prev = free_list->prev;
  chunk->next = free_list;
  free_list = chunk;
}
