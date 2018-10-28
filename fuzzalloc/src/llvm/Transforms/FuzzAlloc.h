//===-- FuzzAlloc.h - Custom memory manager for fuzzing ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This header file defines constants for the custom memory mananger,
/// \p fuzzalloc.
///
//===----------------------------------------------------------------------===//
//
#ifndef _FUZZ_ALLOC_H_
#define _FUZZ_ALLOC_H_

/// Metadata label for storing the total size (in bytes) of a promoted static
/// array
const char *const ARRAY_PROM_SIZE_MD = "static-array-prom.size";

#endif
