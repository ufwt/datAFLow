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

#include "llvm/ADT/Optional.h"

namespace llvm {
class DataLayout;
class StructType;
class Value;
} // namespace llvm

// Tag log strings
const char *const CommentStart = "# ";
const char *const LogSeparator = ",";
const char *const FunctionLogPrefix = "fun";
const char *const GlobalVariableLogPrefix = "gv";
const char *const GlobalAliasLogPrefix = "ga";
const char *const StructOffsetLogPrefix = "struct";

/// A struct type and the offset of an element in that struct
using StructOffset = std::pair<const llvm::StructType *, unsigned>;

/// Like `GetUnderlyingObject` in ValueTracking analysis, except that it looks
/// through load instructions
llvm::Value *GetUnderlyingObjectThroughLoads(llvm::Value *,
                                             const llvm::DataLayout &,
                                             unsigned = 6);

/// Get the offset of the struct element at the given byte offset
///
/// This function will recurse through sub-structs, so the returned struct type
/// may be different from the given struct type.
StructOffset getStructOffset(const llvm::StructType *, unsigned,
                             const llvm::DataLayout &);

/// Retrieve a struct and the offset of an element in that struct from TBAA
/// metadata attached to the given instruction
llvm::Optional<StructOffset> getStructOffsetFromTBAA(const llvm::Instruction *);

#endif // FUZZALLOC_COMMON_H
