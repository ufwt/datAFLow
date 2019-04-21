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

#endif // FUZZALLOC_H
