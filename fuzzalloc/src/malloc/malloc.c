#include <errno.h>      // for errno, ENOMEM
#include <stdint.h>     // for uintptr_t
#include <string.h>     // for memset
#include <sys/mman.h>   // for mmap
#include <unistd.h>     // for getpagesize

#include "fuzzalloc.h"
#include "malloc_internal.h"

/// Maps malloc/calloc tags (inserted during compilation) to allocation-site
/// pools
static uint16_t pool_map[TAG_MAX + 1];

static int page_size;

/// Align the given value
static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

/// Initialize a pool of the given size (mmap'ed memory) with the given initial
/// chunk
//static struct pool_t initialize_pool(size_t allocated_size,
//                                     struct chunk_t *chunk) {
//  struct pool_t pool;
//
//  pool.in_use = TRUE;
//  pool.allocated_size = allocated_size;
//  pool.used_size = CHUNK_SIZE(chunk);
//  pool.entry = chunk;
//
//  return pool;
//}

__attribute__((constructor))
static void __init_fuzzalloc(void) {
    page_size = getpagesize();
}

void *__tagged_malloc(uint16_t tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%u, %lu)\n", tag, size);

  if (size == 0) {
    return NULL;
  }

  if (pool_map[tag] == 0) {
    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site

    // If the requested size cannot fit in an allocation pool, return an error
    if (size + POOL_OVERHEAD > MAX_POOL_SIZE) {
        DEBUG_MSG("memory request too large for an allocation pool\n");
        return NULL;
    }

    // Adjust the allocation size so that it is properly aligned
    // XXX this alignment is too large!
    const size_t aligned_size = align(size + POOL_OVERHEAD, POOL_ALIGN);

    // mmap the requested amount of memory
    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", aligned_size);
    void *base = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (void *)(-1)) {
        DEBUG_MSG("mmap failed: %s\n", strerror(errno));
        return NULL;
    }
    DEBUG_MSG("mmap base at %p\n", base);

    // Adjust the mmap'd memory so that it is aligned with a valid pool ID. If
    // necessary, unmap wasted memory between the original mapping and the
    // aligned mapping. munmap works at a page granularity, meaning all pages
    // containing a part of the indicated range are unmapped. To prevent
    // SIGSEGVs by unmapping a page that is partly used, only unmap at the page
    // level
    void *pool_base = (void*)align((uintptr_t) base, POOL_ALIGN);
    size_t adjust = (pool_base - base) / page_size * page_size;
    if (adjust > 0) {
        DEBUG_MSG("unmapping %lu bytes of unused memory...\n", adjust);
        munmap(base, adjust);
    }

    DEBUG_MSG("allocation pool at %p\n", pool_base);
    DEBUG_MSG("pool size -> %lu bytes\n", aligned_size - adjust);

//    // Adjust the allocation size so that it is properly aligned. If the
//    // requested size cannot fit within the chunk's size field, return an error
//    const size_t aligned_size = align(size + CHUNK_OVERHEAD, CHUNK_ALIGN);
//    if (aligned_size > MAX_CHUNK_SIZE) {
//      DEBUG_MSG("memory request too large\n");
//      return NULL;
//    }
//
//    // XXX is this correct?
//    const size_t mmap_size = POOL_OVERHEAD + (POOL_SIZE_SCALE * aligned_size);
//
//    // mmap the requested memory
//    DEBUG_MSG("mmap-ing %lu bytes of memory...\n", mmap_size);
//    void *base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
//                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//    if (base == (void *)-1) {
//      DEBUG_MSG("mmap failed\n");
//      return NULL;
//    }
//    DEBUG_MSG("mmap base at %p\n", base);
//
//    uintptr_t pool_ptr = (uintptr_t)base + POOL_OVERHEAD + CHUNK_OVERHEAD;
//    size_t adjust = 0;
//
//    // Adjust the mmap'ed memory so that the memory returned to the user is
//    // aligned correctly
//    uintptr_t ptr = (uintptr_t)base + CHUNK_OVERHEAD;
//    size_t adjust = 0;
//    if ((ptr & (SIZE_ALIGN - 1)) != 0) {
//      adjust = SIZE_ALIGN - (ptr & (SIZE_ALIGN - 1));
//      DEBUG_MSG("adjustment of %lu bytes required for mmap base\n", adjust);
//    }
//    ptr += adjust - CHUNK_OVERHEAD;
//
//    // Set the required fields in the allocated chunk
//    struct chunk_t *chunk = (struct chunk_t *)ptr;
//    SET_CHUNK_SIZE(chunk, aligned_size);
//    SET_IN_USE(chunk);
//
//    // This is the first chunk allocated for this particular tag, so save it
//    // into the pool map
//    pool_map[tag] = initialize_pool(mmap_size, chunk);
//
//    DEBUG_MSG("returning %p to the user\n", CHUNK_TO_MEM(chunk));
//    return CHUNK_TO_MEM(chunk);
  } else {
    // Reuse of an existing allocation site. Try and fit the new allocation
    // into the existing memory pool

    DEBUG_MSG("allocation site already in use!\n");
  }

  // Execution should not reach here
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
