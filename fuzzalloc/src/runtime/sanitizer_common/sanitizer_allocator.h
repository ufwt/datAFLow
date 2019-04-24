//===-- sanitizer_allocator.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Specialized memory allocator for ThreadSanitizer, MemorySanitizer, etc.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALLOCATOR_H
#define SANITIZER_ALLOCATOR_H

// clang-format off
#include "sanitizer_internal_defs.h"
#include "sanitizer_common.h"
#include "sanitizer_libc.h"
#include "sanitizer_list.h"
#include "sanitizer_mutex.h"
#include "sanitizer_lfstack.h"
#include "sanitizer_procmaps.h"
// clang-format on

#if FUZZALLOC_ASAN
// As per interception/interception.h, the interceptors must be declared
// outside of the __sanitizer namespace
#include "interception/interception.h"
#include "fuzzalloc.h"

typedef __fuzzalloc::tag_t TAG_T;

extern "C" void *__tagged_malloc(TAG_T tag, SIZE_T size);
extern "C" void free(void *ptr);

DECLARE_REAL_AND_INTERCEPTOR(void *, __tagged_malloc, TAG_T, SIZE_T);
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *);
#endif // FUZZALLOC_ASAN

namespace __sanitizer {

// Allows the tools to name their allocations appropriately.
extern const char *PrimaryAllocatorName;
extern const char *SecondaryAllocatorName;

// Since flags are immutable and allocator behavior can be changed at runtime
// (unit tests or ASan on Android are some examples), allocator_may_return_null
// flag value is cached here and can be altered later.
bool AllocatorMayReturnNull();
void SetAllocatorMayReturnNull(bool may_return_null);

// Returns true if allocator detected OOM condition. Can be used to avoid memory
// hungry operations.
bool IsAllocatorOutOfMemory();
// Should be called by a particular allocator when OOM is detected.
void SetAllocatorOutOfMemory();

void PrintHintAllocatorCannotReturnNull();

// Allocators call these callbacks on mmap/munmap.
struct NoOpMapUnmapCallback {
  void OnMap(uptr p, uptr size) const {}
  void OnUnmap(uptr p, uptr size) const {}
};

// Callback type for iterating over chunks.
typedef void (*ForEachChunkCallback)(uptr chunk, void *arg);

INLINE u32 Rand(u32 *state) { // ANSI C linear congruential PRNG.
  return (*state = *state * 1103515245 + 12345) >> 16;
}

INLINE u32 RandN(u32 *state, u32 n) { return Rand(state) % n; } // [0, n)

template <typename T> INLINE void RandomShuffle(T *a, u32 n, u32 *rand_state) {
  if (n <= 1)
    return;
  u32 state = *rand_state;
  for (u32 i = n - 1; i > 0; i--)
    Swap(a[i], a[RandN(&state, i + 1)]);
  *rand_state = state;
}

// clang-format off
#include "sanitizer_allocator_size_class_map.h"
#include "sanitizer_allocator_stats.h"
#include "sanitizer_allocator_primary64.h"
#include "sanitizer_allocator_bytemap.h"
#include "sanitizer_allocator_local_cache.h"
#include "sanitizer_allocator_primary32.h"
// clang-format on

#if defined(__x86_64) && FUZZALLOC_ASAN
// Only use the fuzzalloc allocator on 64-bit systems
#include "sanitizer_fuzzalloc_allocator.h"
#else
#include "sanitizer_allocator_secondary.h"
#include "sanitizer_allocator_combined.h"
#endif // FUZZALLOC_ASAN

} // namespace __sanitizer

#endif // SANITIZER_ALLOCATOR_H
