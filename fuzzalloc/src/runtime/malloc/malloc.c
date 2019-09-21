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

/// Maps malloc/calloc/realloc def site tags (inserted during compilation) to
/// mspaces
static tag_t def_site_to_mspace_map[TAG_MAX + 1];

// XXX should this be a constant?
static int page_size = 0;

static size_t max_mspace_size = 0;

static ptrdiff_t mspace_overhead = -1;

#if defined(FUZZALLOC_USE_LOCKS)
static pthread_mutex_t malloc_global_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

//===-- Public helper functions -------------------------------------------===//

/// Get the mspace tag associated with the given pointer
tag_t get_mspace_tag(void *p) {
  return (tag_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_TAG_BITS));
}

//===-- Private helper functions ------------------------------------------===//

static size_t init_mspace_size(void) {
  size_t psize = DEFAULT_MSPACE_SIZE;

  char *mspace_size_str = getenv(MSPACE_SIZE_ENV_VAR);
  if (mspace_size_str) {
    char *endptr;
    psize = strtoul(mspace_size_str, &endptr, 0);
    if (psize == 0 || *endptr != '\0' || mspace_size_str == endptr) {
      DEBUG_MSG("unable to read %s environment variable: %s\n",
                MSPACE_SIZE_ENV_VAR, mspace_size_str);
      psize = DEFAULT_MSPACE_SIZE;
    }
  } else {
    DEBUG_MSG("%s not set. Using default mspace size\n", MSPACE_SIZE_ENV_VAR);
  }

  DEBUG_MSG("using mspace size %lu bytes\n", psize);
  return psize;
}

static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

static mspace create_fuzzalloc_mspace(tag_t def_site_tag) {
  // This should only happen once
  if (!page_size) {
    page_size = getpagesize();
  }

  // This should also only happen once
  //
  // XXX When used with ASan and this is first called, environ does not seem
  // to have been initialized yet, so we'll always use the default mspace size
  if (!max_mspace_size) {
    max_mspace_size = init_mspace_size();
  }

  // This def site has not been used before. Create a new mspace for this site
  DEBUG_MSG("creating new mspace\n");

  // Adjust the allocation size so that it is properly aligned
  size_t mspace_size = align(max_mspace_size, MSPACE_ALIGNMENT);

  // mmap the requested amount of memory. Note that the pages mapped will be
  // inaccessible until we mprotect them
  DEBUG_MSG("mmap-ing %lu bytes of memory...\n", mspace_size);
  void *mmap_base =
      mmap(NULL, mspace_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mmap_base == (void *)(-1)) {
    DEBUG_MSG("mmap failed: %s\n", strerror(errno));
    errno = ENOMEM;

    // Returning - must release the global lock
    RELEASE_MALLOC_GLOBAL_LOCK();

    return NULL;
  }
  DEBUG_MSG("mmap base at %p\n", mmap_base);

  // Adjust the mmap'd memory so that it is aligned with a valid mspace tag. If
  // necessary, unmap wasted memory between the original mapping and the
  // aligned mapping. munmap works at a page granularity, meaning all pages
  // containing a part of the indicated range are unmapped. To prevent
  // SIGSEGVs by unmapping a page that is partly used, only unmap at the page
  // level
  void *mspace_base = (void *)align((uintptr_t)mmap_base, MSPACE_ALIGNMENT);

  ptrdiff_t adjust = (mspace_base - mmap_base) / page_size * page_size;
  if (adjust > 0) {
    munmap(mmap_base, adjust);
    DEBUG_MSG("unmapped %lu bytes before mspace base (new mspace base at %p)\n",
              adjust, mspace_base);

    mspace_size -= adjust;
  }

  mspace_size = mspace_size > max_mspace_size ? max_mspace_size : mspace_size;

  // Make the mapped pages accessible
  if (mprotect(mspace_base, mspace_size, PROT_READ | PROT_WRITE)) {
    DEBUG_MSG("mprotect failed for mspace at %p (length %lu bytes)\n",
              mspace_base, mspace_size);
    abort();
  }

  DEBUG_MSG("creating mspace with base %p (size %lu bytes)\n", mspace_base,
            mspace_size);
#if defined(FUZZALLOC_USE_LOCKS)
  mspace space = create_mspace_with_base(mspace_base, mspace_size, TRUE);
#else
  mspace space = create_mspace_with_base(mspace_base, mspace_size, FALSE);
#endif
  if (!space) {
    DEBUG_MSG("create_mspace_with_base failed at base %p (size %lu bytes)\n",
              mspace_base, mspace_size);
    abort();
  }

  // This should also, also only happen once
  if (mspace_overhead == -1) {
    mspace_overhead = space - mspace_base;
    DEBUG_MSG("mspace overhead is %lu bytes\n", mspace_overhead);
  }

  // This is the first memory allocation for this def site, so save the mspace
  // tag into the mspace map (and likewise the def site tag into the def site
  // map)
  tag_t mspace_tag = get_mspace_tag(space);
  DEBUG_MSG("mspace %#x (size %lu bytes) created for def site %#x\n",
            mspace_tag, mspace_size, def_site_tag);
  def_site_to_mspace_map[def_site_tag] = mspace_tag;

  return space;
}

//===-- malloc interface --------------------------------------------------===//

void *__tagged_malloc(tag_t def_site_tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%#x, %lu) called from %p\n", def_site_tag, size,
            __builtin_return_address(0));

  void *mem = NULL;

  // Need to ensure that no-one else can update the def site to mspace mapping
  // while we are using it
  ACQUIRE_MALLOC_GLOBAL_LOCK();
  tag_t mspace_tag = def_site_to_mspace_map[def_site_tag];

  if (mspace_tag == 0) {
    mspace space = create_fuzzalloc_mspace(def_site_tag);

    // Release the global lock - we've updated the def site to mspace mapping
    RELEASE_MALLOC_GLOBAL_LOCK();

    mem = mspace_malloc(space, size);
    DEBUG_MSG("mspace_malloc(%p, %lu) returned %p\n", space, size, mem);
  } else {
    // Don't need the global lock anymore - the mspace lock will take care of it
    RELEASE_MALLOC_GLOBAL_LOCK();

    // Reuse of an existing def site. Try and fit the new memory request into
    // the existing mspace
    assert(mspace_overhead >= 0);
    mspace space = GET_MSPACE(mspace_tag) + mspace_overhead;

    mem = mspace_malloc(space, size);
    DEBUG_MSG("mspace_malloc(%p, %lu) returned %p\n", space, size, mem);
  }

  return mem;
}

void *__tagged_calloc(tag_t def_site_tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%#x, %lu, %lu) called from %p\n", def_site_tag,
            nmemb, size, __builtin_return_address(0));

  mspace space;
  tag_t mspace_tag = def_site_to_mspace_map[def_site_tag];

  if (mspace_tag == 0) {
    space = create_fuzzalloc_mspace(def_site_tag);
  } else {
    space = GET_MSPACE(mspace_tag) + mspace_overhead;
  }

  void *mem = mspace_calloc(space, nmemb, size);
  DEBUG_MSG("mspace_calloc(%p, %lu, %lu) returned %p\n", space, nmemb, size,
            mem);

  return mem;
}

void *__tagged_realloc(tag_t def_site_tag, void *ptr, size_t size) {
  DEBUG_MSG("__tagged_realloc(%#x, %p, %lu) called from %p\n", def_site_tag,
            ptr, size, __builtin_return_address(0));

  mspace space;

  if (!ptr) {
    space = create_fuzzalloc_mspace(def_site_tag);
  } else {
    tag_t mspaceag = get_mspace_tag(ptr);
    space = GET_MSPACE(mspaceag) + mspace_overhead;
  }

  void *mem = mspace_realloc(space, ptr, size);
  DEBUG_MSG("mspace_realloc(%p, %p, %lu) returned %p\n", space, ptr, size, mem);

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
  tag_t mspace_tag = get_mspace_tag(ptr);
  mspace space = GET_MSPACE(mspace_tag) + mspace_overhead;

  DEBUG_MSG("mspace_free(%p, %p)\n", space, ptr);
  return mspace_free(space, ptr);
}
