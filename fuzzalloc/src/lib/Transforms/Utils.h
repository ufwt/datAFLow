//===-- Utils.h - LLVM transform utils ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// LLVM transform utility functions.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_UTILS_H
#define FUZZALLOC_UTILS_H

namespace llvm {
class DataLayout;
class Value;
} // namespace llvm

/// Like `GetUnderlyingObject` in ValueTracking analysis, except that it looks
/// through load instructions
llvm::Value *GetUnderlyingObjectThroughLoads(llvm::Value *,
                                             const llvm::DataLayout &,
                                             unsigned = 6);

#endif // FUZZALLOC_UTILS_H
