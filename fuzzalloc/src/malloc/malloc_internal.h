#ifndef _MALLOC_INTERNAL_H_
#define _MALLOC_INTERNAL_H_

#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <stdint.h>

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
  /// Size of the previous chunk (in bytes)
  size_t prev_size;
  /// Current chunk size (in bytes). The least-significant bit indicates
  /// whether the chunk is in use or free
  size_t size;
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

/// Set the chunk as being in use
#define SET_CHUNK_IN_USE(c) (c->size |= ((size_t)1))

/// Set the chunk as being free
#define SET_CHUNK_FREE(c) (c->size &= ((size_t)(~1)))

/// Set the previous chunk as being in use
#define SET_PREV_CHUNK_IN_USE(c) (c->prev_size |= ((size_t)1))

/// Set the previous chunk as being free
#define SET_PREV_CHUNK_FREE(c) (c->prev_size &= ((size_t)(~1)))

/// Chunk size (in bytes), ignoring the in use/free bit
#define CHUNK_SIZE(c) (c->size & ((size_t)(~0) - 1))

/// Previous chunk size (in bytes), ignoring the in use/free bit
#define PREV_CHUNK_SIZE(c) (c->prev_size & ((size_t)(~0) - 1))

/// Set the size of the chunk (in bytes). This size should include the chunk
/// overhead. The caller must ensure that the in-use bit is unused
#define SET_CHUNK_SIZE(c, s) (c->size = (size_t)((s) & (~1)))

/// Set the size of the previous chunk (in bytes). This size should include
/// the chunk overhead. The caller must ensure that the in-use bit is unused
#define SET_PREV_CHUNK_SIZE(c, s) (c->prev_size = (size_t)((s) & (~1)))

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
#define POOL_OVERHEAD (sizeof(struct chunk_t*))

/// Default pool size. Configurable at compile-time
#ifndef POOL_SIZE
#define POOL_SIZE 20000UL
#endif

/// The number of usable bits on the X86-64 architecture
#define NUM_USABLE_BITS 48

/// The number of (most upper) bits used to identify an allocation site's pool
#define NUM_POOL_ID_BITS 16

/// Pool alignment. This ensures that the upper \p NUM_POOL_ID_BITS of the pool
/// address are unique to a single pool
#define POOL_ALIGNMENT (1UL << (NUM_USABLE_BITS - NUM_POOL_ID_BITS))

/// Extract the pool identifier from the allocated pool
#define GET_POOL_ID(p) ((uint16_t)((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_POOL_ID_BITS)))

/// Get the allocation pool address from an identifier
#define GET_POOL(id)                                                           \
  ((struct pool_t *)((uintptr_t)id << (NUM_USABLE_BITS - NUM_POOL_ID_BITS)))

///////////////////////////////////////////////////////////////////////////////

#endif
