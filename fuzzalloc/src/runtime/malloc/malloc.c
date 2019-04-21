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

/// Maps malloc/calloc/realloc allocation call site tags (inserted during
/// compilation) to allocation pool tags
static tag_t alloc_site_to_pool_map[TAG_MAX + 1];

// XXX should this be a constant?
static int page_size = 0;

static size_t max_pool_size = 0;

#if defined(FUZZALLOC_USE_LOCKS)
static pthread_mutex_t malloc_global_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// Defined in deref.c
extern tag_t __pool_to_alloc_site_map[TAG_MAX + 1];

//===-- Private helper functions ------------------------------------------===//

// Forward declaration
tag_t get_pool_tag(void *p);

#if !defined(NDEBUG)
static inline void print_free_list(const struct pool_t *pool) {
  struct chunk_t *chunk = pool->free_list;

  DEBUG_MSG("pool %#x free list\n", get_pool_tag((void *)pool));
  while (chunk) {
    assert(CHUNK_FREE(chunk));
    DEBUG_MSG("  - %p (size %lu bytes)\n", chunk, CHUNK_SIZE(chunk));
    chunk = chunk->next;
  }
}
#endif

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
  }

  DEBUG_MSG("using pool size %lu bytes\n", psize);
  return psize;
}

static inline uintptr_t align(uintptr_t n, size_t alignment) {
  return (n + alignment - 1) & -alignment;
}

static inline struct chunk_t *find_free_chunk(const struct pool_t *pool,
                                              size_t size) {
  struct chunk_t *chunk = pool->free_list;

  while (chunk && CHUNK_SIZE(chunk) < size) {
    assert(CHUNK_FREE(chunk));
    chunk = chunk->next;
  }

  return chunk;
}

static void unlink_chunk_from_free_list(struct pool_t *pool,
                                        struct chunk_t *chunk) {
  // Unlink the chunk from the free list and update its pointers to point to
  // the next/previous free chunks in the list
  if (chunk->prev) {
    chunk->prev->next = chunk->next;
  }
  if (chunk->next) {
    chunk->next->prev = chunk->prev;
  }

  // If the unlinked chunk was the head of the free list, update the free
  // list to start at the next chunk in the list
  if (chunk == pool->free_list) {
    pool->free_list = chunk->next;
  }
}

/// Return non-zero if the given chunk is within the limits of the given
/// allocation pool
static inline bool_t within_allocation_pool(const struct pool_t *pool,
                                            const struct chunk_t *chunk) {
  return (ptrdiff_t)chunk < (ptrdiff_t)pool + pool->size;
}

static inline void in_use_sanity_check(struct chunk_t *chunk) {
  size_t chunk_size = CHUNK_SIZE(chunk);
  struct chunk_t *next_chunk = NEXT_CHUNK(chunk);
  size_t next_prev_chunk_size = PREV_CHUNK_SIZE(next_chunk);

  if (!CHUNK_IN_USE(chunk)) {
    DEBUG_MSG("chunk %p should be marked in use\n", chunk);
    goto do_abort;
  }

  if (CHUNK_IN_USE(chunk) != PREV_CHUNK_IN_USE(next_chunk)) {
    DEBUG_MSG("chunk %p in use must be the same as chunk %p in use\n", chunk,
              next_chunk);
    goto do_abort;
  }

  if (!PREV_CHUNK_IN_USE(next_chunk)) {
    DEBUG_MSG("next chunk %p should mark previous chunk %p in use\n",
              next_chunk, chunk);
    goto do_abort;
  }

  // XXX check `NEXT_CHUNK(PREV_CHUNK(chunk)) == chunk)` ?

  if (chunk_size != next_prev_chunk_size) {
    DEBUG_MSG("chunk %p (size %lu) should equal next chunk %p previous (size "
              "%lu bytes)\n",
              chunk, chunk_size, next_chunk, next_prev_chunk_size);
    goto do_abort;
  }

  return;

do_abort:
  abort();
}

//===-- Public helper functions -------------------------------------------===//

tag_t get_pool_tag(void *p) {
  return (tag_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_TAG_BITS));
}

size_t get_pool_size(void *p) { return GET_POOL(get_pool_tag(p))->size; }

//===-- malloc interface --------------------------------------------------===//

void *__tagged_malloc(tag_t alloc_site_tag, size_t size) {
  DEBUG_MSG("__tagged_malloc(%#x, %lu) called from %p\n", alloc_site_tag, size,
            __builtin_return_address(0));

  if (size == 0) {
    return NULL;
  }

  void *mem = NULL;
  size_t req_chunk_size = align(size + IN_USE_CHUNK_OVERHEAD, CHUNK_ALIGNMENT);

  // Need to ensure that no-one else can update the allocation site to pool
  // tag mapping while we are using it
  ACQUIRE_MALLOC_GLOBAL_LOCK();
  tag_t pool_tag = alloc_site_to_pool_map[alloc_site_tag];

  if (pool_tag == 0) {
    // This should only happen once
    if (!page_size) {
      page_size = getpagesize();
    }

    // This should also only happen once
    if (!max_pool_size) {
      max_pool_size = init_pool_size();
    }

    // This allocation site has not been used before. Create a new "allocation
    // pool" for this site
    DEBUG_MSG("creating new allocation pool\n");

    // If the requested size cannot fit in an allocation pool, return an error
    if (size > max_pool_size) {
      DEBUG_MSG("memory request too large for an allocation pool\n");
      errno = EINVAL;
      return NULL;
    }

    // Adjust the allocation size so that it is properly aligned
    size_t pool_size = align(size + POOL_OVERHEAD, POOL_ALIGNMENT);

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

    // Make the mapped pages accessible
    if (mprotect(pool_base, max_pool_size, PROT_READ | PROT_WRITE)) {
      DEBUG_MSG("mprotect failed for pool at %p (length %lu bytes)\n",
                pool_base, max_pool_size);
      abort();
    }

    DEBUG_MSG("allocation pool base at %p (size %lu bytes)\n", pool_base,
              pool_size);

    // Create an entry chunk that can hold the amount of data requested by the
    // user. Chunks have their own alignment constraints that must satisfy the
    // malloc API
    struct chunk_t *chunk = MEM_TO_CHUNK(
        align((uintptr_t)pool_base + POOL_OVERHEAD + IN_USE_CHUNK_OVERHEAD,
              CHUNK_ALIGNMENT));
    SET_CHUNK_SIZE(chunk, req_chunk_size);
    SET_CHUNK_IN_USE(chunk);

    // Mark the "previous" chunk of the entry chunk as in use so that we will
    // never try to coalesce it and break something
    SET_PREV_CHUNK_IN_USE(chunk);

    // Create the initial free chunk following the newly-created entry chunk.
    // This chunk will later be insert into the allocation pool's free list
    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);

    // The initial free chunk should not be outside of the allocation pool
    assert((ptrdiff_t)free_chunk < (ptrdiff_t)pool_base + pool_size);

    SET_CHUNK_SIZE(free_chunk, max_pool_size - req_chunk_size);
    SET_CHUNK_FREE(free_chunk);
    SET_PREV_CHUNK_IN_USE(free_chunk);

    free_chunk->prev = free_chunk->next = NULL;

    // Finally, initialise the allocation pool's metadata and insert the free
    // chunk into the pool's free list. Initialize the pool's lock (if it has
    // one)
    struct pool_t *pool = (void *)pool_base;
    pool->size = max_pool_size;
    pool->entry = chunk;
    pool->free_list = free_chunk;
    INIT_POOL_LOCK(pool);

    DEBUG_MSG("initial chunk created at %p (size %lu bytes)\n", pool->entry,
              req_chunk_size);
    DEBUG_MSG("free list head at %p (chunk size %lu bytes)\n", pool->free_list,
              CHUNK_SIZE(free_chunk));

    // This is the first memory allocation for this allocation site, so save
    // the allocation pool tag into the pool map (and likewise the allocation
    // site tag into the site map)
    pool_tag = get_pool_tag(pool_base);
    DEBUG_MSG("pool %#x (size %lu bytes) created for allocation site %#x\n",
              pool_tag, pool_size, alloc_site_tag);
    alloc_site_to_pool_map[alloc_site_tag] = pool_tag;
    __pool_to_alloc_site_map[pool_tag] = alloc_site_tag;

    // Release the global lock - we've updated the allocation site to pool tag
    // mapping
    RELEASE_MALLOC_GLOBAL_LOCK();

    mem = CHUNK_TO_MEM(chunk);
  } else {
    // Don't need the global lock anymore - can just use the pool's lock
    RELEASE_MALLOC_GLOBAL_LOCK();

    struct pool_t *pool = GET_POOL(pool_tag);
    ACQUIRE_POOL_LOCK(pool);

    // Reuse of an existing allocation site. Try and fit the new memory request
    // into the existing allocation pool
    DEBUG_MSG("reusing pool %p (allocation site %#x)\n", pool, alloc_site_tag);

    // Find a suitably-sized free chunk in the allocation pool for this tag
    struct chunk_t *chunk = find_free_chunk(pool, req_chunk_size);
    if (!chunk) {
      DEBUG_MSG("unable to find a free chunk (min. size %lu bytes) in "
                "allocation pool %#x\n",
                req_chunk_size, pool_tag);
      // TODO grow the allocation pool via mprotect
      abort();
    }
    size_t free_chunk_size = CHUNK_SIZE(chunk);

    // Save a pointer to the chunk after the current chunk BEFORE we resize the
    // current chunk
    struct chunk_t *next_chunk = NEXT_CHUNK(chunk);

    // Mark the chunk as being in use
    SET_CHUNK_SIZE(chunk, req_chunk_size);
    SET_CHUNK_IN_USE(chunk);

    struct chunk_t *free_chunk = NEXT_CHUNK(chunk);
    assert(within_allocation_pool(pool, free_chunk));
    SET_PREV_CHUNK_IN_USE(free_chunk);

    DEBUG_MSG("chunk created at %p (size %lu bytes)\n", chunk, req_chunk_size);

    // If we haven't used the entire newly-allocated chunk, turn whatever space
    // is left following it into a new free chunk. Insert this free chunk at the
    // head of the allocation pool's free list.
    //
    // If we cannot fit the free chunk metadata in the free space then there
    // isn't much we can do - just leave it.
    //
    // XXX duplicate code in __tagged_realloc
    if (free_chunk_size - req_chunk_size > FREE_CHUNK_OVERHEAD) {
      SET_CHUNK_SIZE(free_chunk, free_chunk_size - req_chunk_size);
      SET_CHUNK_FREE(free_chunk);

      // Unlink the chunk from the free list and update its pointers to point to
      // the new free chunk
      if (chunk->prev) {
        chunk->prev->next = free_chunk;
      }
      if (chunk->next) {
        chunk->next->prev = free_chunk;
      }

      // If the unlinked chunk was the head of the free list, update the free
      // list to start at the next chunk in the list
      if (chunk == pool->free_list) {
        pool->free_list = free_chunk;
      }

      // Place the free chunk in the free list
      free_chunk->prev = chunk->prev;
      free_chunk->next = chunk->next;

      DEBUG_MSG("new free chunk at %p (size %lu bytes)\n", free_chunk,
                CHUNK_SIZE(free_chunk));
    } else {
      DEBUG_MSG(
          "%lu bytes is not enough space to create a new free chunk at %p\n",
          free_chunk_size - req_chunk_size, free_chunk);

      // If there is any free space left in the chunk, there is not much we can
      // do here except mark the chunk as being in use - the chunk is too small
      // to hold all the required metadata of a free chunk :(
      if (free_chunk_size > req_chunk_size) {
        SET_CHUNK_SIZE(free_chunk, free_chunk_size - req_chunk_size);
        SET_CHUNK_IN_USE(free_chunk);
        SET_PREV_CHUNK_IN_USE(next_chunk);
      }

      // Unlink the chunk from the free list
      unlink_chunk_from_free_list(pool, chunk);
    }

    // Release the pool's lock - we've updated the pool
    RELEASE_POOL_LOCK(pool);

    mem = CHUNK_TO_MEM(chunk);
  }

  return mem;
}

void *__tagged_calloc(tag_t alloc_site_tag, size_t nmemb, size_t size) {
  DEBUG_MSG("__tagged_calloc(%#x, %lu, %lu) called from %p\n", alloc_site_tag,
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

// Forward declaration
void free(void *p);

void *__tagged_realloc(tag_t alloc_site_tag, void *ptr, size_t size) {
  DEBUG_MSG("__tagged_realloc(%#x, %p, %lu) called from %p\n", alloc_site_tag,
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

    struct pool_t *pool = GET_POOL(get_pool_tag(orig_chunk));
    ACQUIRE_POOL_LOCK(pool);

    struct chunk_t *next_chunk = NEXT_CHUNK(orig_chunk);
    assert(within_allocation_pool(pool, next_chunk));

    // Sanity check that the chunk metadata hasn't been corrupted in some way
    in_use_sanity_check(orig_chunk);

    size_t orig_chunk_size = CHUNK_SIZE(orig_chunk);
    size_t new_chunk_size =
        align(size + IN_USE_CHUNK_OVERHEAD, CHUNK_ALIGNMENT);

    if (orig_chunk_size == new_chunk_size) {
      // Nothing to resize - short-circuit

      RELEASE_POOL_LOCK(pool);

      DEBUG_MSG("realloc size is the same as the original size (%lu bytes)\n",
                orig_chunk_size);

      mem = CHUNK_TO_MEM(orig_chunk);
    } else if (orig_chunk_size > new_chunk_size) {
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
      assert(within_allocation_pool(pool, free_chunk));

      SET_CHUNK_SIZE(free_chunk, orig_chunk_size - new_chunk_size);
      SET_CHUNK_FREE(free_chunk);
      SET_PREV_CHUNK_IN_USE(free_chunk);

      free_chunk->prev = pool->free_list->prev;
      free_chunk->next = pool->free_list;
      pool->free_list = free_chunk;

      // We've resized the chunk and updated the pool - release the lock
      RELEASE_POOL_LOCK(pool);

      DEBUG_MSG("existing chunk at %p (size %lu bytes) resized to %lu bytes\n",
                orig_chunk, orig_chunk_size, new_chunk_size);

      mem = CHUNK_TO_MEM(orig_chunk);
    } else {
      // The requested reallocation size is larger than the space currently
      // allocated for this chunk. Find a free chunk in the same allocation
      // pool that will fit the new chunk. Free the old chunk
      DEBUG_MSG("increasing chunk %p size from %lu bytes to %lu bytes\n",
                orig_chunk, orig_chunk_size, new_chunk_size);

      // Find a suitably-sized free chunk in the allocation pool for the
      // reallocation destination
      struct chunk_t *new_chunk = find_free_chunk(pool, new_chunk_size);
      if (!new_chunk) {
        DEBUG_MSG("unable to find a free chunk (min. size %lu bytes) in "
                  "allocation pool %#x\n",
                  new_chunk_size, get_pool_tag(pool));
        // TODO grow the allocation pool via mprotect
        abort();
      }
      size_t free_chunk_size = CHUNK_SIZE(new_chunk);

      // Save a pointer to the chunk after the new chunk BEFORE we resize the
      // new chunk
      struct chunk_t *next_new_chunk = NEXT_CHUNK(new_chunk);

      // Mark the chunk as being in use
      SET_CHUNK_SIZE(new_chunk, new_chunk_size);
      SET_CHUNK_IN_USE(new_chunk);

      struct chunk_t *free_chunk = NEXT_CHUNK(new_chunk);
      SET_PREV_CHUNK_IN_USE(free_chunk);

      // Turn whatever free space (if any) is left following the newly-allocated
      // chunk into a new free chunk. Insert this free chunk into the allocation
      // pool's free list. If there is no free space just unlink the chunk from
      // the free list
      //
      // XXX duplicate code from __tagged_malloc
      if (free_chunk_size - new_chunk_size > FREE_CHUNK_OVERHEAD) {
        SET_CHUNK_SIZE(free_chunk, free_chunk_size - new_chunk_size);
        SET_CHUNK_FREE(free_chunk);

        // Unlink the chunk from the free list and update its pointers to point
        // to the newly-freed space following the new chunk
        if (new_chunk->prev) {
          new_chunk->prev->next = free_chunk;
        }
        if (new_chunk->next) {
          new_chunk->next->prev = free_chunk;
        }

        // If the unlinked chunk was the head of the free list, update the free
        // list to start at the free space directly following the
        // newly-allocated chunk
        if (new_chunk == pool->free_list) {
          pool->free_list = free_chunk;
        }

        // Place the free chunk in the free list
        free_chunk->prev = new_chunk->prev;
        free_chunk->next = new_chunk->next;
      } else {
        DEBUG_MSG(
            "%lu bytes is not enough space to create a new free chunk at %p\n",
            free_chunk_size - new_chunk_size, free_chunk);

        // If there is any free space left in the chunk, there is not much we
        // can do here except mark the chunk as being in use - the chunk is too
        // small to hold all the required metadata of a free chunk :(
        if (free_chunk_size > new_chunk_size) {
          SET_CHUNK_SIZE(free_chunk, free_chunk_size - new_chunk_size);
          SET_CHUNK_IN_USE(free_chunk);
          SET_PREV_CHUNK_IN_USE(next_new_chunk);
        }

        // Unlink the new chunk from the free list
        unlink_chunk_from_free_list(pool, new_chunk);
      }

      // Move the data contained in the original chunk into the new chunk
      DEBUG_MSG("moving chunk data from %p to %p (%lu bytes)\n",
                (uint8_t *)orig_chunk + IN_USE_CHUNK_OVERHEAD,
                (uint8_t *)new_chunk + IN_USE_CHUNK_OVERHEAD,
                orig_chunk_size - IN_USE_CHUNK_OVERHEAD);
      memcpy((uint8_t *)new_chunk + IN_USE_CHUNK_OVERHEAD,
             (uint8_t *)orig_chunk + IN_USE_CHUNK_OVERHEAD,
             orig_chunk_size - IN_USE_CHUNK_OVERHEAD);

      // Mark the original chunk as free and insert it at the head of the free
      // list
      SET_CHUNK_FREE(orig_chunk);
      SET_PREV_CHUNK_FREE(next_chunk);

      // TODO coalesce orig_chunk with its neighbouring chunks (if possible)

      orig_chunk->prev = pool->free_list->prev;
      orig_chunk->next = pool->free_list;
      pool->free_list->prev = orig_chunk;
      pool->free_list = orig_chunk;

      // Release the pool's lock - we're done
      RELEASE_POOL_LOCK(pool);

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

  struct pool_t *pool = GET_POOL(get_pool_tag(chunk));
  ACQUIRE_POOL_LOCK(pool);

  DEBUG_MSG("freeing memory at %p (size %lu bytes) from pool %#x\n", chunk,
            chunk_size, get_pool_tag(pool));

  // Sanity check that the chunk metadata hasn't been corrupted in some way
  in_use_sanity_check(chunk);

  struct chunk_t *next_chunk = NEXT_CHUNK(chunk);
  assert(within_allocation_pool(pool, next_chunk));

  // If the previous chunk is free, coalesce
  if (PREV_CHUNK_FREE(chunk)) {
    struct chunk_t *prev_chunk = PREV_CHUNK(chunk);
    DEBUG_MSG(
        "previous chunk at %p (size %lu bytes) is also free - coalescing\n",
        prev_chunk, PREV_CHUNK_SIZE(chunk));

    // Unlink the previous free chunk from the free list (because it is getting
    // merged into the soon-to-be free chunk)
    unlink_chunk_from_free_list(pool, prev_chunk);

    // Free chunks must always be preceeded by an in use chunk
    //
    // XXX must coalesce when realloc-ing to maintain this invariant
    // if (PREV_CHUNK_FREE(prev_chunk)) {
    //  DEBUG_MSG("free chunks must always be preceeded by an in use chunk "
    //            "(chunk %p is free)\n",
    //            PREV_CHUNK(prev_chunk));
    //  abort();
    //}

    chunk_size += PREV_CHUNK_SIZE(chunk);
    chunk = prev_chunk;
  }

  // If the next chunk is free, coalesce
  if (CHUNK_FREE(next_chunk)) {
    DEBUG_MSG("next chunk at %p (size %lu bytes) is also free - coalescing\n",
              next_chunk, CHUNK_SIZE(next_chunk));

    chunk_size += CHUNK_SIZE(next_chunk);

    // Unlink the next free chunk from the free list (because it is getting
    // merged into the soon-to-be free chunk)
    unlink_chunk_from_free_list(pool, next_chunk);

    next_chunk = NEXT_CHUNK(next_chunk);

    if (!within_allocation_pool(pool, next_chunk)) {
      // We've gone beyond the pool's limit
      next_chunk = NULL;
    } else {
      // Free chunks must always be followed by an in use chunk
      //
      // XXX must coalesce when realloc-ing to maintain this invariant
      // if (CHUNK_FREE(next_chunk)) {
      //  DEBUG_MSG("free chunks must always be followed by an in use chunk "
      //            "(chunk %p is free)\n",
      //            next_chunk);
      //  abort();
      //}
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
  // list. If we coalesced with the previous chunk, then this chunk may already
  // be the list head. This would create a circularly-linked list if we were to
  // add the same chunk as the new head, as free_list->head->next would be the
  // same as free_list->head
  if (pool->free_list != chunk) {
    chunk->prev = NULL;
    chunk->next = pool->free_list;

    if (pool->free_list) {
      pool->free_list->prev = chunk;
    }
    pool->free_list = chunk;
  }

  RELEASE_POOL_LOCK(pool);
}
