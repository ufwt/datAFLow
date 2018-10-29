#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "fuzzalloc.h"
#include "malloc_internal.h"

/// Maps malloc/calloc tags (inserted during compilation) to allocation-site
/// pools
static struct pool_t pool_map[TAG_MAX + 1];

/// Align the given value
static inline size_t align(size_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

/// Initialize a pool of the given size (mmap'ed memory) with the given initial
/// chunk
static struct pool_t initialize_pool(size_t allocated_size,
                                     struct chunk_t *chunk) {
  struct pool_t pool;

  pool.in_use = TRUE;
  pool.allocated_size = allocated_size;
  pool.used_size = CHUNK_SIZE(chunk);
  pool.entry = chunk;

  return pool;
}

void *__tagged_malloc(uint16_t tag, size_t size) {
  DEBUG_MSG("malloc(%lu)\n", size);

  if (size == 0) {
    return NULL;
  }

  if (pool_map[tag].in_use == FALSE) {
    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site

    // Adjust the allocation size so that it is properly aligned. If the
    // requested size cannot fit within the chunk's size field, return an error
    const size_t aligned_size = align(size + CHUNK_OVERHEAD, SIZE_ALIGN);
    if (aligned_size > MAX_CHUNK_SIZE) {
      DEBUG_MSG("memory request too large\n");
      return NULL;
    }

    // XXX is this even right?
    const size_t mmap_size = POOL_SIZE_SCALE * aligned_size;

    // mmap the requested memory
    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", mmap_size);
    void *base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (void *)-1) {
      DEBUG_MSG("mmap failed\n");
      return NULL;
    }
    DEBUG_MSG("mmap base at %p\n", base);

    // Adjust the mmap'ed memory so that the memory returned to the user is
    // aligned correctly
    uintptr_t ptr = (uintptr_t)base + CHUNK_OVERHEAD;
    size_t adjust = 0;
    if ((ptr & (SIZE_ALIGN - 1)) != 0) {
      adjust = SIZE_ALIGN - (ptr & (SIZE_ALIGN - 1));
      DEBUG_MSG("adjustment of %lu bytes required for mmap base\n", adjust);
    }
    ptr += adjust - CHUNK_OVERHEAD;

    // Set the required fields in the allocated chunk
    struct chunk_t *chunk = (struct chunk_t *)ptr;
    SET_CHUNK_SIZE(chunk, aligned_size);
    SET_IN_USE(chunk);

    // This is the first chunk allocated for this particular tag, so save it
    // into the pool map
    pool_map[tag] = initialize_pool(mmap_size, chunk);

    DEBUG_MSG("returning %p to the user\n", CHUNK_TO_MEM(chunk));
    return CHUNK_TO_MEM(chunk);
  } else {
    // Reuse of an existing allocation site. Try and fit the new allocation
    // into the existing memory pool

    DEBUG_MSG("allocation site already in use!\n");
  }

  // Execution should not reach here
  return NULL;
}

void *__tagged_calloc(uint16_t tag, size_t nmemb, size_t size) {
  DEBUG_MSG("calloc(%lu, %lu)\n", nmemb, size);

  // Adapted from muslc

  if (size && nmemb > (size_t)(-1) / size) {
    errno = ENOMEM;
    return NULL;
  }

  size *= nmemb;
  void *p = __tagged_malloc(tag, size);
  if (!p) {
    return p;
  }

  return memset(p, 0, size);
}

void *malloc(size_t size) { return __tagged_malloc(DEFAULT_TAG, size); }

void *calloc(size_t nmemb, size_t size) {
  return __tagged_calloc(DEFAULT_TAG, nmemb, size);
}

void *realloc(void *ptr, size_t size) { return ptr; }

void free(void *ptr) {
  DEBUG_MSG("free(%p)\n", ptr);

  if (!ptr) {
    return;
  }

  // TODO implement free
}
