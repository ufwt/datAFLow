#include <assert.h>   // for assert
#include <errno.h>    // for errno, ENOMEM
#include <stddef.h>   // for ptrdiff_t
#include <stdint.h>   // for uintptr_t
#include <string.h>   // for memset
#include <sys/mman.h> // for mmap
#include <unistd.h>   // for getpagesize

#include "fuzzalloc.h"
#include "malloc_internal.h"

/// Maps malloc/calloc tags (inserted during compilation) to allocation-site
/// pools
static uint16_t pool_map[TAG_MAX + 1];

static int page_size;

__attribute__((constructor)) static void __init_fuzzalloc(void) {
  page_size = getpagesize();
}

static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

static inline struct chunk_t *find_first_free_chunk(const struct pool_t *pool) {
  uintptr_t pool_end = (uintptr_t)pool + pool->allocated_size;
  struct chunk_t *chunk = pool->entry;

  while (CHUNK_IN_USE(chunk)) {
    chunk = NEXT_CHUNK(chunk);

    // Using this chunk will overflow the allocation pool
    if ((uintptr_t)chunk + CHUNK_OVERHEAD > pool_end) {
      return NULL;
    }
  }

  return chunk;
}

void *__tagged_malloc(uint16_t tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%u, %lu)\n", tag, size);

  if (size == 0) {
    return NULL;
  }

  size_t chunk_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGN);
  uint16_t pool_id = pool_map[tag];

  if (pool_id == 0) {
    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site

    // If the requested size cannot fit in an allocation pool, return an error
    if (size > DEFAULT_POOL_SIZE) {
      DEBUG_MSG("memory request too large for an allocation pool\n");
      errno = EINVAL;
      return NULL;
    }

    // Adjust the allocation size so that it is properly aligned
    size_t pool_size = align(size + POOL_OVERHEAD, POOL_ALIGN);

    // mmap the requested amount of memory
    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", pool_size);
    void *base = mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (void *)(-1)) {
      DEBUG_MSG("mmap failed: %s\n", strerror(errno));
      errno = ENOMEM;

      return NULL;
    }
    DEBUG_MSG("mmap base at %p\n", base);

    // Adjust the mmap'd memory so that it is aligned with a valid pool ID. If
    // necessary, unmap wasted memory between the original mapping and the
    // aligned mapping. munmap works at a page granularity, meaning all pages
    // containing a part of the indicated range are unmapped. To prevent
    // SIGSEGVs by unmapping a page that is partly used, only unmap at the page
    // level
    void *pool_base = (void *)align((uintptr_t)base, POOL_ALIGN);
    ptrdiff_t adjust = (pool_base - base) / page_size * page_size;
    if (adjust > 0) {
      munmap(base, adjust);
      DEBUG_MSG("unmapped %lu bytes before pool base\n", adjust);

      pool_size -= adjust;
    }

    // To ensure alignment with a valid pool ID, we mmap much more memory than
    // we actually require. Adjust the amount of mmap'd memory to the default
    // pool size
    adjust =
        (pool_size - DEFAULT_POOL_SIZE - POOL_OVERHEAD) * page_size / page_size;
    if (adjust > 0) {
      munmap(pool_base + (pool_size - adjust), adjust);
      DEBUG_MSG("unmapped %lu bytes of unused memory\n", adjust);

      pool_size -= adjust;
    }

    // Create an entry chunk that can hold the amount of data requested by the
    // user. Chunks have their own alignment constraints that must satisfy the
    // malloc API
    struct chunk_t *chunk = MEM_TO_CHUNK(align(
        (uintptr_t)pool_base + POOL_OVERHEAD + CHUNK_OVERHEAD, CHUNK_ALIGN));
    SET_CHUNK_SIZE(chunk, chunk_size);
    SET_CHUNK_IN_USE(chunk);

    // Create a free chunk following the newly-created entry chunk
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    free_chunk->prev = free_chunk->next = NULL;
    SET_CHUNK_SIZE(free_chunk, pool_size - chunk_size);
    CLEAR_CHUNK_USE(free_chunk);
    SET_PREV_CHUNK_SIZE(free_chunk, chunk_size);
    SET_PREV_CHUNK_IN_USE(free_chunk);

    DEBUG_MSG("chunk created at %p (size %lu)\n", chunk, chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    // Finally, initialise the allocation pool's metadata. The allocated size
    // is the amount of mmap'd data left after aligning and cleaning up the
    // mapping. The used size is the amount of mmap'd space that this first
    // chunk will use (including storing the pool metadata itself). The entry
    // chunk is the chunk we just created
    struct pool_t *pool = (void *)pool_base;
    pool->allocated_size = pool_size;
    pool->entry = chunk;

    // This is the first memory allocation for this allocation site, so save
    // the pool ID into the pool map
    pool_id = GET_POOL_ID(pool_base);
    DEBUG_MSG("tag %u -> pool ID 0x%x\n", tag, pool_id);
    pool_map[tag] = pool_id;

    return CHUNK_TO_MEM(chunk);
  } else {
    // Reuse of an existing allocation site. Try and fit the new memory request
    // into the existing allocation pool

    struct pool_t *pool = GET_POOL(pool_id);

    // Find a suitably-sized free chunk in the allocation pool for this tag
    struct chunk_t *chunk = find_first_free_chunk(pool);
    for (; chunk != NULL && CHUNK_SIZE(chunk) < chunk_size;
         chunk = chunk->next) {
    }
    if (!chunk) {
      DEBUG_MSG("unable to find a free chunk in the allocation pool\n");
      errno = ENOMEM;
      return NULL;
    }
    size_t free_chunk_size = CHUNK_SIZE(chunk);

    SET_CHUNK_SIZE(chunk, chunk_size);
    SET_CHUNK_IN_USE(chunk);

    // Create a free chunk following the newly-created chunk
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    free_chunk->prev = chunk->prev;
    free_chunk->next = chunk->next;
    SET_CHUNK_SIZE(free_chunk, free_chunk_size - chunk_size);
    CLEAR_CHUNK_USE(free_chunk);
    SET_PREV_CHUNK_SIZE(free_chunk, chunk_size);
    SET_PREV_CHUNK_IN_USE(free_chunk);

    // TODO coalesce free chunk with neighbouring free chunks

    DEBUG_MSG("chunk created at %p (size %lu)\n", chunk, chunk_size);
    DEBUG_MSG("next free chunk at %p (size %lu)\n", free_chunk,
              CHUNK_SIZE(free_chunk));

    return CHUNK_TO_MEM(chunk);
  }

  assert(FALSE && "Execution should never reach here");
  return NULL;
}

void *__tagged_calloc(uint16_t tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%u, %lu, %lu)\n", tag, nmemb, size);

  // Adapted from muslc

  if (size && nmemb > (size_t)(-1) / size) {
    errno = ENOMEM;
    return NULL;
  }

  size *= nmemb;
  void *ptr = __tagged_malloc(tag, size);
  if (!ptr) {
    return ptr;
  }

  return memset(ptr, 0, size);
}

void *malloc(size_t size) { return __tagged_malloc(DEFAULT_TAG, size); }

void *calloc(size_t nmemb, size_t size) {
  return __tagged_calloc(DEFAULT_TAG, nmemb, size);
}

void *realloc(void *ptr, size_t size) {
  DEBUG_MSG("realloc(%p, %lu)\n", ptr, size);

  // TODO implement realloc
  return NULL;
}

void free(void *ptr) {
  DEBUG_MSG("free(%p)\n", ptr);

  if (!ptr) {
    return;
  }

  // TODO implement free
}
