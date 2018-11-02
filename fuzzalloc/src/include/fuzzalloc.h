//===-- fuzzalloc.h - The fuzzing memory allocator ----------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Header file for the fuzzing memory allocator, \p fuzzalloc.
///
//===----------------------------------------------------------------------===//

#ifndef _FUZZ_ALLOC_H_
#define _FUZZ_ALLOC_H_

/// Number of bits in a tag
#define TAG_NUM_BITS 16

/// Tag typedef
typedef uint16_t tag_t;

/// The maximum possible tag value
#define TAG_MAX ((1 << TAG_NUM_BITS) - 1)

/// The default malloc/calloc tag. Used by default for non-instrumented code
#define DEFAULT_TAG 0

#endif
