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
#if defined(SANITIZER_ALLOCATOR_H)
// Need a separate namespace so that we don't conflict with tag_t in hwasan
namespace __fuzzalloc {
#endif // SANITIZER_ALLOCATOR_H

extern "C" {
#endif // __cplusplus

/// The number of usable bits on the X86-64 architecture
#define NUM_USABLE_BITS 48

/// Number of bits in a tag
#define NUM_TAG_BITS 16

/// Tag type
typedef uint16_t tag_t;

/// The maximum possible tag value
#define TAG_MAX ((1 << NUM_TAG_BITS) - 1)

/// The default malloc/calloc/realloc tag. Used by default for non-instrumented
/// code
#define DEFAULT_TAG 0

/// Extract the pool tag from the allocated pool
tag_t get_pool_tag(void *p);

/// Get the size of the pool the given allocation belongs to
size_t get_pool_size(void *p);

#if defined(__cplusplus)
}

#if defined(SANITIZER_ALLOCATOR_H)
} // __fuzzalloc
#endif // SANITIZER_ALLOCATOR_H
#endif // __cplusplus

#endif // FUZZALLOC_H
