//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#ifndef MALLOC_INTERNAL_H
#define MALLOC_INTERNAL_H

#if !defined(__x86_64__)
#error Unsupported platform
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(FUZZALLOC_USE_LOCKS)
#include <pthread.h>
#endif

#include "fuzzalloc.h"    // for tag_t
#include "malloc-2.8.3.h" // for mspaces

typedef uint8_t bool_t;

#define FALSE 0
#define TRUE !FALSE

//===-- Locks -------------------------------------------------------------===//

#if defined(FUZZALLOC_USE_LOCKS)
#define ACQUIRE_MALLOC_GLOBAL_LOCK() (pthread_mutex_lock(&malloc_global_mutex))
#define RELEASE_MALLOC_GLOBAL_LOCK()                                           \
  (pthread_mutex_unlock(&malloc_global_mutex))
#else // No locking
#define ACQUIRE_MALLOC_GLOBAL_LOCK()
#define RELEASE_MALLOC_GLOBAL_LOCK()
#endif // defined(FUZZALLOC_USE_LOCKS)

//===-- mspace ------------------------------------------------------------===//

/// Default mspace size (in bytes). Configurable at run-time via an environment
/// variable
#define MSPACE_DEFAULT_SIZE 500000000UL

/// The mspace size environment variable
#define MSPACE_SIZE_ENV_VAR "FUZZALLOC_MSPACE_SIZE"

/// Mspace alignment. This ensures that the upper \p NUM_TAG_BITS of the mspace
/// address are unique to a single mspace
#define MSPACE_ALIGNMENT (1UL << (NUM_USABLE_BITS - NUM_TAG_BITS))

/// Get the mspace address from an def site tag
#define GET_MSPACE(tag)                                                        \
  ((void *)(((uintptr_t)(tag)) << (NUM_USABLE_BITS - NUM_TAG_BITS)))

#endif // MALLOC_INTERNAL_H
