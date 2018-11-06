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

#include <stdint.h>

#include "fuzzalloc.h"

// TODO delete
#define DEBUG 1

#ifdef DEBUG
#include <assert.h>
#include <stdio.h>

#define DEBUG_MSG(format, ...)                                                 \
  fprintf(stderr, "%s(): " format, __func__, ##__VA_ARGS__)
#else
#define DEBUG_MSG(format, ...)
#define assert(x)
#endif

#define FALSE 0
#define TRUE !FALSE

///////////////////////////////////////////////////////////////////////////////

struct chunk_t {
  /// Size of the previous chunk (in bytes). The least-significant bit
  /// indicates whether the previous chunk is in use or free. This size
  /// includes the overhead associated with the chunk
  size_t prev_size;
  /// Current chunk size (in bytes). The least-significant bit indicates
  /// whether the chunk is in use or free. This size includes the overhead
  /// associated with the chunk
  size_t size;

  // The struct elements below are only used when the chunk is free

  /// Pointer to the next free chunk
  struct chunk_t *next;
  /// Pointer to the previous free chunk
  struct chunk_t *prev;
};

/// Size of chunk overhead (in bytes)
#define CHUNK_OVERHEAD (2 * sizeof(size_t))

/// Malloc alignment (as used by dlmalloc)
#define CHUNK_ALIGNMENT ((size_t)(2 * sizeof(void *)))

/// Returns non-zero if the chunk is in use
#define CHUNK_IN_USE(c) ((uint8_t)(c->size & ((size_t)1)))

// Returns non-zero if the chunk is free
#define CHUNK_FREE(c) (!CHUNK_IN_USE(c))

/// Returns non-zero if the previous chunk is in use
#define PREV_CHUNK_IN_USE(c) ((uint8_t)(c->prev_size & ((size_t)1)))

/// Returns non-zero if the previous chunk is free
#define PREV_CHUNK_FREE(c) (!PREV_CHUNK_IN_USE(c))

/// Set the chunk as being in use
#define SET_CHUNK_IN_USE(c) (c->size |= ((size_t)1))

/// Set the chunk as being free
#define SET_CHUNK_FREE(c) (c->size &= ((size_t)(~1)))

/// Set the previous chunk as being in use
#define SET_PREV_CHUNK_IN_USE(c) (c->prev_size |= ((size_t)1))

/// Set the previous chunk as being free
#define SET_PREV_CHUNK_FREE(c) (c->prev_size &= ((size_t)(~1)))

/// Chunk size (in bytes), ignoring the in use/free bit
#define CHUNK_SIZE(c) (c->size & ((size_t)(~1)))

/// Previous chunk size (in bytes), ignoring the in use/free bit
#define PREV_CHUNK_SIZE(c) (c->prev_size & ((size_t)(~1)))

/// Set the size (in bytes) of a chunk and mark it as being in use. This size
/// must include the chunk overhead
#define SET_CHUNK_IN_USE_SIZE(c, s) (c->size = (size_t)((s) | 1))

/// Set the size (in bytes) of the previous chunk and mark it as being in use.
/// This size must include the chunk overhead
#define SET_PREV_CHUNK_IN_USE_SIZE(c, s) (c->prev_size = (size_t)((s) | 1))

/// Set the size (in bytes) of a chunk and mark it as being free. This size
/// must include the chunk overhead
#define SET_CHUNK_FREE_SIZE(c, s) (c->size = (size_t)((s) & (~1)))

/// Set the size (in bytes) of the previous chunk and mark it as being free.
/// This size must include the chunk overhead
#define SET_PREV_CHUNK_FREE_SIZE(c, s) (c->prev_size = (size_t)((s) & (~1)))

/// Pointer to next chunk
#define NEXT_CHUNK(c) ((struct chunk_t *)((uint8_t *)(c) + CHUNK_SIZE(c)))

/// Pointer to previous chunk
#define PREV_CHUNK(c) ((struct chunk_t *)((uint8_t *)(c)-CHUNK_PREV_SIZE(c)))

/// Convert memory address (as seen by the user) to a chunk
#define MEM_TO_CHUNK(p) ((struct chunk_t *)((uint8_t *)(p)-CHUNK_OVERHEAD))

/// Convert chunk to a memory address (as seen by the user)
#define CHUNK_TO_MEM(c) ((void *)((uint8_t *)(c) + CHUNK_OVERHEAD))

///////////////////////////////////////////////////////////////////////////////

struct pool_t {
  /// Free list for this pool
  struct chunk_t *free_list;
  /// First chunk in this pool
  struct chunk_t *entry;
};

/// Size of pool overhead (in bytes)
#define POOL_OVERHEAD (sizeof(struct chunk_t *))

/// Default pool size. Configurable at compile-time
#ifndef POOL_SIZE
#define POOL_SIZE 20000UL
#endif

/// Pool alignment. This ensures that the upper \p NUM_TAG_BITS of the pool
/// address are unique to a single pool
#define POOL_ALIGNMENT (1UL << (NUM_USABLE_BITS - NUM_TAG_BITS))

/// Extract the pool identifier from the allocated pool
#define GET_POOL_ID(p)                                                         \
  ((uint16_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_TAG_BITS)))

/// Get the allocation pool address from an identifier
#define GET_POOL(id)                                                           \
  ((struct pool_t *)((uintptr_t)id << (NUM_USABLE_BITS - NUM_TAG_BITS)))

///////////////////////////////////////////////////////////////////////////////

#endif
