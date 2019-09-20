//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#ifndef FUZZALLOC_H
#define FUZZALLOC_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#if FUZZALLOC_ASAN
// Need a separate namespace so that we don't conflict with tag_t in hwasan
namespace __fuzzalloc {
#endif // FUZZALLOC_ASAN

extern "C" {
#endif // __cplusplus

/// The number of usable bits on the X86-64 architecture
#define NUM_USABLE_BITS 48

/// Number of bits in a tag
#define NUM_TAG_BITS 16

/// Tag type
typedef uint16_t tag_t;

/// The default def site tag. Used by default for non-instrumented code
#define DEFAULT_TAG 0

/// ASan's quarantine region gets its own mspace
#define QUARANTINE_TAG 1

/// LLVM instrumentation can start using tags starting from this value
#define INST_TAG_START 2

/// The maximum possible tag value
#define TAG_MAX ((tag_t)~0)

/// Extract the mspace tag from the mspace pool
tag_t get_mspace_tag(void *p);

#if defined(__cplusplus)
}

#if FUZZALLOC_ASAN
} // namespace __fuzzalloc
#endif // FUZZALLOC_ASAN
#endif // __cplusplus

#endif // FUZZALLOC_H
