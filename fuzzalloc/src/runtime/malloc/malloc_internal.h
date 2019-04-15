//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#ifndef _MALLOC_INTERNAL_H_
#define _MALLOC_INTERNAL_H_

#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(FUZZALLOC_USE_LOCKS)
#include <pthread.h>
#endif

#include "fuzzalloc.h" // for tag_t

typedef uint8_t bool_t;

#define FALSE 0
#define TRUE !FALSE

#define SIZE_T_SIZE (sizeof(size_t))

//===-- Malloc chunk format -----------------------------------------------===//

struct chunk_t {
  /// Size of the previous chunk (in bytes) if free. This size includes the
  /// overhead associated with the chunk
  size_t prev_size;
  /// Current chunk size (in bytes). The least-significant bit indicates
  /// whether the previous chunk is in use (1) or free (0), while the second
  /// least-significant byte indicates whether this chunk is in use (1) or free
  /// (0). This size includes the overhead associated with the chunk
  size_t size;

  // The struct elements below are only used when the chunk is free

  /// Pointer to the next free chunk
  struct chunk_t *next;
  /// Pointer to the previous free chunk
  struct chunk_t *prev;
};

/// Size of in-use chunk overhead (in bytes)
#define IN_USE_CHUNK_OVERHEAD (2 * SIZE_T_SIZE)

/// Size of free chunk overhead (in bytes)
#define FREE_CHUNK_OVERHEAD (sizeof(struct chunk_t))

/// Malloc alignment (same as dlmalloc)
#define CHUNK_ALIGNMENT ((size_t)(2 * sizeof(void *)))

/// Bit offset used to indicate that the previous chunk is in use
#define PREV_CHUNK_IN_USE_BIT (1)

/// Bit offset used to indicate that the current chunk is in use
#define CHUNK_IN_USE_BIT (2)

/// Chunk usage bits
#define IN_USE_BITS (PREV_CHUNK_IN_USE_BIT | CHUNK_IN_USE_BIT)

/// Returns non-zero if the chunk is in use
#define CHUNK_IN_USE(c)                                                        \
  ((uint8_t)(((c)->size & CHUNK_IN_USE_BIT)) >> (CHUNK_IN_USE_BIT - 1))

// Returns non-zero if the chunk is free
#define CHUNK_FREE(c) (!CHUNK_IN_USE(c))

/// Returns non-zero if the previous chunk is in use
#define PREV_CHUNK_IN_USE(c)                                                   \
  ((uint8_t)(((c)->size & PREV_CHUNK_IN_USE_BIT)) >>                           \
   (PREV_CHUNK_IN_USE_BIT - 1))

/// Returns non-zero if the previous chunk is free
#define PREV_CHUNK_FREE(c) (!PREV_CHUNK_IN_USE(c))

/// Set the size of the current chunk. This also updates its footer. The chunk
/// in use and previous chunk in use bits remain unchanged
#define SET_CHUNK_SIZE(c, s)                                                   \
  do {                                                                         \
    (c)->size = ((c)->size & IN_USE_BITS) | (s);                               \
    (((struct chunk_t *)((uint8_t *)(c) + (s)))->prev_size = (s));             \
  } while (0);

/// Mark the chunk as in use
#define SET_CHUNK_IN_USE(c) ((c)->size |= CHUNK_IN_USE_BIT)

/// Mark the previous chunk as in use
#define SET_PREV_CHUNK_IN_USE(c) ((c)->size |= PREV_CHUNK_IN_USE_BIT)

/// Mark the chunk as free
#define SET_CHUNK_FREE(c) ((c)->size &= ~CHUNK_IN_USE_BIT)

/// Mark the previous chunk as free
#define SET_PREV_CHUNK_FREE(c) ((c)->size &= ~PREV_CHUNK_IN_USE_BIT)

/// Chunk size (in bytes), ignoring the in use bits
#define CHUNK_SIZE(c) ((c)->size & (size_t)(~IN_USE_BITS))

/// Previous chunk size (in bytes)
#define PREV_CHUNK_SIZE(c) ((c)->prev_size)

/// Pointer to next chunk
#define NEXT_CHUNK(c) ((struct chunk_t *)((uint8_t *)(c) + CHUNK_SIZE(c)))

/// Pointer to previous chunk
#define PREV_CHUNK(c) ((struct chunk_t *)((uint8_t *)(c) - ((c)->prev_size)))

/// Convert chunk to a memory address (as seen by the user)
#define CHUNK_TO_MEM(c) ((void *)((uint8_t *)(c) + IN_USE_CHUNK_OVERHEAD))

/// Convert memory address (as seen by the user) to a chunk
#define MEM_TO_CHUNK(p)                                                        \
  ((struct chunk_t *)((uint8_t *)(p)-IN_USE_CHUNK_OVERHEAD))

//===-- Locks -------------------------------------------------------------===//

#if defined(FUZZALLOC_USE_LOCKS)
#define INIT_POOL_LOCK(p) (pthread_mutex_init(&((p)->lock), NULL))
#define ACQUIRE_POOL_LOCK(p) (pthread_mutex_lock(&((p)->lock)))
#define RELEASE_POOL_LOCK(p) (pthread_mutex_unlock(&((p)->lock)))

#define ACQUIRE_MALLOC_GLOBAL_LOCK() (pthread_mutex_lock(&malloc_global_mutex))
#define RELEASE_MALLOC_GLOBAL_LOCK()                                           \
  (pthread_mutex_unlock(&malloc_global_mutex))
#else // No locking
#define INIT_POOL_LOCK(p)
#define ACQUIRE_POOL_LOCK(p)
#define RELEASE_POOL_LOCK(p)

#define ACQUIRE_MALLOC_GLOBAL_LOCK()
#define RELEASE_MALLOC_GLOBAL_LOCK()
#endif // defined(FUZZALLOC_USE_LOCKS)

//===-- Allocation pool format --------------------------------------------===//

struct pool_t {
#if defined(FUZZALLOC_USE_LOCKS)
  /// Lock for accessing this pool
  pthread_mutex_t lock;
#endif

  /// Pool size (in bytes). This includes the overhead of this header
  size_t size;
  /// Free list for this pool
  struct chunk_t *free_list;
  /// First chunk in this pool
  struct chunk_t *entry;
};

/// Size of pool overhead (in bytes)
#define POOL_OVERHEAD (sizeof(struct pool_t) - sizeof(struct chunk_t *))

/// Default pool size (in bytes). Configurable at run-time via an environment
/// variable
#define DEFAULT_POOL_SIZE 100000UL

/// The pool size environment variable
#define POOL_SIZE_ENV_VAR "FUZZALLOC_POOL_SIZE"

/// Pool alignment. This ensures that the upper \p NUM_TAG_BITS of the pool
/// address are unique to a single pool
#define POOL_ALIGNMENT (1UL << (NUM_USABLE_BITS - NUM_TAG_BITS))

/// Extract the pool tag from the allocated pool
#define GET_POOL_TAG(p)                                                        \
  ((tag_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_TAG_BITS)))

/// Get the allocation pool address from an allocation pool tag
#define GET_POOL(tag)                                                          \
  ((struct pool_t *)((uintptr_t)tag << (NUM_USABLE_BITS - NUM_TAG_BITS)))

#endif // _MALLOC_INTERNAL_H_
