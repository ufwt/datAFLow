#ifndef _MALLOC_INTERNAL_H_
#define _MALLOC_INTERNAL_H_

#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <stdint.h>

// TODO delete
#define DEBUG 1

#ifdef DEBUG
#include <stdio.h>
#define DEBUG_MSG(format, ...)                                                 \
  fprintf(stderr, "%s(): " format, __func__, ##__VA_ARGS__)
#else
#define DEBUG_MSG(format, ...)
#endif

#define FALSE 0
#define TRUE !FALSE

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

///////////////////////////////////////////////////////////////////////////////

struct chunk_t {
  /// Chunk size (in bytes). The least-significant bit indicates whether the
  /// chunk is in use or free
  size_t size;
  /// Pointer to the next free chunk
  struct chunk_t *next;
};

/// Size of chunk overhead (in bytes)
#define CHUNK_OVERHEAD (1 * sizeof(size_t))

// Chunk alignment used by muslc
// XXX why this value?
#define CHUNK_ALIGN (4 * sizeof(size_t))

/// Returns non-zero if the chunk is in use
#define IN_USE(c) (c->size & ((size_t)1))

/// Set the chunk as being in use
#define SET_IN_USE(c) (c->size |= ((size_t)1))

/// Set the chunk as being free
#define CLEAR_USE(c) (c->size &= ((size_t)(~1)))

/// Chunk size (in bytes), ignoring the in use/free bit
#define CHUNK_SIZE(c) (c->size & ((size_t)(~0) - 1))

/// Set the size of the chunk
#define SET_CHUNK_SIZE(c, s)                                                   \
  (c->size = (size_t)(s - (CHUNK_ALIGN - CHUNK_OVERHEAD)))

/// The maximum possible chunk size (in bytes)
#define MAX_CHUNK_SIZE SIZE_MAX

/// Pointer to next chunk
#define NEXT_CHUNK(c) ((struct chunk *)((uint8_t *)(c) + CHUNK_SIZE(c)))

/// Convert memory address (as seen by the user) to a chunk
#define MEM_TO_CHUNK(p) ((struct chunk *)((uint8_t *)(p)-CHUNK_OVERHEAD))

/// Convert chunk to a memory address (as seen by the user)
#define CHUNK_TO_MEM(c) ((void *)((uint8_t *)(c) + CHUNK_OVERHEAD))

///////////////////////////////////////////////////////////////////////////////

struct pool_t {
  /// Total size (in bytes) allocated (via mmap) for this pool
  size_t allocated_size;
  /// Size (in bytes) already used in this pool
  size_t used_size;
  /// First chunk in this pool
  struct chunk_t entry;
};

/// Size of pool overhead (in bytes)
#define POOL_OVERHEAD (2 * sizeof(size_t))

/// The number of usable bits on the X86-64 architecture
#define NUM_USABLE_BITS 48

/// The number of (most upper) bits used to identify an allocation site's pool
#define NUM_POOL_ID_BITS 16

/// Pool alignment. This ensures that the upper \p NUM_POOL_ID_BITS of the pool
/// address are unique to a single pool
#define POOL_ALIGN (1UL << (NUM_USABLE_BITS - NUM_POOL_ID_BITS))

/// Maximum size (in bytes) of a pool: either the amount of data we can fit
/// after \p POOL_ID_BITS or the maximum value we can represent in \p size_t
#define MAX_POOL_SIZE MIN(POOL_ALIGN - 1, SIZE_MAX)

/// Extract the pool identifier from the allocated pool
#define GET_POOL_ID(p) ((uintptr_t)(p) >> (NUM_USABLE_BITS - NUM_POOL_ID_BITS))

///////////////////////////////////////////////////////////////////////////////

#endif
