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

/// The default malloc/calloc tag. Used by default for non-instrumented code.
#define DEFAULT_TAG 0
