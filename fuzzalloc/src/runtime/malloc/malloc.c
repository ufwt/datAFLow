//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#include <errno.h>    // for errno, EINVAL, ENOMEM
#include <stddef.h>   // for ptrdiff_t
#include <stdint.h>   // for uintptr_t
#include <stdlib.h>   // for abort, getenv
#include <string.h>   // for memcpy, memset
#include <sys/mman.h> // for mmap
#include <unistd.h>   // for getpagesize

#include "debug.h"
#include "malloc_internal.h"

//===-- Global variables --------------------------------------------------===//

/// Maps malloc/calloc/realloc allocation def site tags (inserted during
/// compilation) to allocation pool tags
static tag_t alloc_site_to_pool_map[TAG_MAX + 1];

// XXX should this be a constant?
static int page_size = 0;

static size_t max_pool_size = 0;

static ptrdiff_t pool_overhead = -1;

#if defined(FUZZALLOC_USE_LOCKS)
static pthread_mutex_t malloc_global_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/// Maps allocation pool tags (created during malloc/calloc/reallocs) to
/// allocation def site tags (inserted during compilation).
///
/// The pointer is needed so that we can access the map from LLVM
/// instrumentation.
tag_t __pool_to_alloc_site_map[TAG_MAX + 1];
tag_t *__pool_to_alloc_site_map_ptr = __pool_to_alloc_site_map;

//===-- Public helper functions -------------------------------------------===//

/// Get the allocation pool tag associated with the given pointer
tag_t get_pool_tag(void *p) {
  return (tag_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_TAG_BITS));
}

//===-- Private helper functions ------------------------------------------===//

static size_t init_pool_size() {
  size_t psize = DEFAULT_POOL_SIZE;

  char *pool_size_str = getenv(POOL_SIZE_ENV_VAR);
  if (pool_size_str) {
    char *endptr;
    psize = strtoul(pool_size_str, &endptr, 0);
    if (psize == 0 || *endptr != '\0' || pool_size_str == endptr) {
      DEBUG_MSG("unable to read %s environment variable: %s\n",
                POOL_SIZE_ENV_VAR, pool_size_str);
      psize = DEFAULT_POOL_SIZE;
    }
  } else {
    DEBUG_MSG("%s not set. Using default pool size\n", POOL_SIZE_ENV_VAR);
  }

  DEBUG_MSG("using pool size %lu bytes\n", psize);
  return psize;
}

static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

static pool_t create_pool(tag_t alloc_site_tag) {
  // This should only happen once
  if (!page_size) {
    page_size = getpagesize();
  }

  // This should also only happen once
  //
  // XXX When used with ASan and this is first called, environ does not seem
  // to have been initialized yet, so we'll always use the default pool size
  if (!max_pool_size) {
    max_pool_size = init_pool_size();
  }

  // This allocation site has not been used before. Create a new "allocation
  // pool" for this site
  DEBUG_MSG("creating new allocation pool\n");

  // Adjust the allocation size so that it is properly aligned
  size_t pool_size = align(max_pool_size, POOL_ALIGNMENT);

  // mmap the requested amount of memory. Note that the pages mapped will be
  // inaccessible until we mprotect them
  DEBUG_MSG("mmap-ing %lu bytes of memory...\n", pool_size);
  void *mmap_base =
      mmap(NULL, pool_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mmap_base == (void *)(-1)) {
    DEBUG_MSG("mmap failed: %s\n", strerror(errno));
    errno = ENOMEM;

    // Returning - must release the global lock
    RELEASE_MALLOC_GLOBAL_LOCK();

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
    DEBUG_MSG("unmapped %lu bytes before pool base (new pool base at %p)\n",
              adjust, pool_base);

    pool_size -= adjust;
  }

  pool_size = pool_size > max_pool_size ? max_pool_size : pool_size;

  // Make the mapped pages accessible
  if (mprotect(pool_base, pool_size, PROT_READ | PROT_WRITE)) {
    DEBUG_MSG("mprotect failed for pool at %p (length %lu bytes)\n", pool_base,
              pool_size);
    abort();
  }

  DEBUG_MSG("creating mspace with base %p (size %lu bytes)\n", pool_base,
            pool_size);
#if defined(FUZZALLOC_USE_LOCKS)
  pool_t pool = create_mspace_with_base(pool_base, pool_size, TRUE);
#else
  pool_t pool = create_mspace_with_base(pool_base, pool_size, FALSE);
#endif
  if (!pool) {
    DEBUG_MSG("create_mspace_with_base failed at base %p (size %lu bytes)\n",
              pool_base, pool_size);
    abort();
  }

  // This should also, also only happen once
  if (pool_overhead == -1) {
    pool_overhead = pool - pool_base;
    DEBUG_MSG("pool overhead is %lu bytes\n", pool_overhead);
  }

  // This is the first memory allocation for this allocation site, so save
  // the allocation pool tag into the pool map (and likewise the allocation
  // site tag into the site map)
  tag_t pool_tag = get_pool_tag(pool);
  DEBUG_MSG("pool %#x (size %lu bytes) created for allocation site %#x\n",
            pool_tag, pool_size, alloc_site_tag);
  alloc_site_to_pool_map[alloc_site_tag] = pool_tag;
  __pool_to_alloc_site_map[pool_tag] = alloc_site_tag;

  return pool;
}

//===-- malloc interface --------------------------------------------------===//

void *__tagged_malloc(tag_t alloc_site_tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%#x, %lu) called from %p\n", alloc_site_tag, size,
            __builtin_return_address(0));

  void *mem = NULL;

  // Need to ensure that no-one else can update the allocation site to pool
  // tag mapping while we are using it
  ACQUIRE_MALLOC_GLOBAL_LOCK();
  tag_t pool_tag = alloc_site_to_pool_map[alloc_site_tag];

  if (pool_tag == 0) {
    pool_t pool = create_pool(alloc_site_tag);

    // Release the global lock - we've updated the allocation site to pool tag
    // mapping
    RELEASE_MALLOC_GLOBAL_LOCK();

    mem = mspace_malloc(pool, size);
    DEBUG_MSG("mspace_malloc(%p, %lu) returned %p\n", pool, size, mem);
  } else {
    // Don't need the global lock anymore - the mspace lock will take care of it
    RELEASE_MALLOC_GLOBAL_LOCK();

    // Reuse of an existing allocation site. Try and fit the new memory request
    // into the existing allocation pool
    assert(pool_overhead >= 0);
    pool_t pool = GET_POOL(pool_tag) + pool_overhead;

    mem = mspace_malloc(pool, size);
    DEBUG_MSG("mspace_malloc(%p, %lu) returned %p\n", pool, size, mem);
  }

  return mem;
}

void *__tagged_calloc(tag_t alloc_site_tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%#x, %lu, %lu) called from %p\n", alloc_site_tag,
            nmemb, size, __builtin_return_address(0));

  pool_t pool;
  tag_t pool_tag = alloc_site_to_pool_map[alloc_site_tag];

  if (pool_tag == 0) {
    pool = create_pool(alloc_site_tag);
  } else {
    pool = GET_POOL(pool_tag) + pool_overhead;
  }

  void *mem = mspace_calloc(pool, nmemb, size);
  DEBUG_MSG("mspace_calloc(%p, %lu, %lu) returned %p\n", pool, nmemb, size,
            mem);

  return mem;
}

void *__tagged_realloc(tag_t alloc_site_tag, void *ptr, size_t size) {
  DEBUG_MSG("__tagged_realloc(%#x, %p, %lu) called from %p\n", alloc_site_tag,
            ptr, size, __builtin_return_address(0));

  pool_t pool;

  if (!ptr) {
    pool = create_pool(alloc_site_tag);
  } else {
    tag_t pool_tag = get_pool_tag(ptr);
    pool = GET_POOL(pool_tag) + pool_overhead;
  }

  void *mem = mspace_realloc(pool, ptr, size);
  DEBUG_MSG("mspace_realloc(%p, %p, %lu) returned %p\n", pool, ptr, size, mem);

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
  tag_t pool_tag = get_pool_tag(ptr);
  pool_t pool = GET_POOL(pool_tag) + pool_overhead;

  DEBUG_MSG("mspace_free(%p, %p)\n", pool, ptr);
  return mspace_free(pool, ptr);
}
