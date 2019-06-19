//===-- Common.h - LLVM transform utils -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_COMMON_H
#define FUZZALLOC_COMMON_H

namespace llvm {
class DataLayout;
class StructType;
class Value;
} // namespace llvm

/// A struct type and an offset into that struct
using StructOffset = std::pair<const llvm::StructType *, unsigned>;

/// Like `GetUnderlyingObject` in ValueTracking analysis, except that it looks
/// through load instructions
llvm::Value *GetUnderlyingObjectThroughLoads(llvm::Value *,
                                             const llvm::DataLayout &,
                                             unsigned = 6);

#endif // FUZZALLOC_COMMON_H
