//
// fuzzalloc
// A memory allocator for fuzzing
//
// Author: Adrian Herrera
//

#ifndef _FUZZ_ALLOC_H_
#define _FUZZ_ALLOC_H_

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

#endif
