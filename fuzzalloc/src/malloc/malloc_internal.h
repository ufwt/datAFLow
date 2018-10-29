//===-- malloc_internal.h - Definitions ---------------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Internal definitions for the fuzzing memory allocator, \p fuzzalloc.
///
//===----------------------------------------------------------------------===//

// TODO delete
#define DEBUG 1

#ifdef DEBUG
#include <stdio.h>
#define DEBUG_MSG(format, ...)                                                 \
  fprintf(stderr, "%s(): " format, __func__, ##__VA_ARGS__)
#else
#define DEBUG_MSG(format, ...)
#endif

// Simple booleans
#define FALSE 0
#define TRUE !FALSE

// Alignment used by muslc
#define SIZE_ALIGN (4 * sizeof(size_t))
#define SIZE_MASK (-SIZE_ALIGN)

/// When a allocation site pool is initially created, make it this times as
/// many bytes larger than the original request (to handle subsequent
/// allocations at the same allocation site)
#define POOL_SIZE_SCALE 4

struct chunk_t {
  size_t size;          //< LSB indicates whether in use or free
  struct chunk_t *next; //< Pointer to next free chunk
};

struct pool_t {
  uint8_t in_use : 1; //< Set to \p TRUE when this pool is in use
  struct {
    size_t allocated_size; //< Total size allocated (via mmap) for this pool
    size_t used_size;      //< Size used in this pool
    struct chunk_t *entry; //< Pointer to the first chunk in this pool
  };
};

/// Amount of chunk overhead (in bytes)
#define CHUNK_OVERHEAD (sizeof(struct chunk_t))

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
  (c->size = (size_t)(s - (SIZE_ALIGN - CHUNK_OVERHEAD)))

/// The maximum possible chunk size (in bytes)
#define MAX_CHUNK_SIZE ((size_t)(~0) - 1)

/// Pointer to next chunk
#define NEXT_CHUNK(c) ((struct chunk *)((uint8_t *)(c) + CHUNK_SIZE(c)))

/// Convert memory address (as seen by the user) to a chunk
#define MEM_TO_CHUNK(p) ((struct chunk *)((uint8_t *)(p)-CHUNK_OVERHEAD))

/// Convert chunk to a memory address (as seen by the user)
#define CHUNK_TO_MEM(c) ((void *)((uint8_t *)(c) + CHUNK_OVERHEAD))
