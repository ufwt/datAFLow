//===-- Utils.h - fuzzalloc LLVM pass utilities -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This header file defines helper functions for \p fuzzalloc's LLVM passes.
///
//===----------------------------------------------------------------------===//

#ifndef _UTILS_H_
#define _UTILS_H_

namespace llvm {
class AllocaInst;
class ReturnInst;
} // namespace llvm

/// Metadata label for storing the total number of elements in a promoted
/// static array
const char *const ARRAY_PROM_NUM_ELEMS_MD = "static-array-prom.numElems";

/// \p free the given allocation before the given return address
void insertFree(llvm::AllocaInst *Alloca, llvm::ReturnInst *Return);

#endif
