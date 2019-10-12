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
class Module;
class Statistic;
class StructType;
class Value;
} // namespace llvm

// Tag log strings
const std::string CommentStart = "# ";
const std::string LogSeparator = ";";
const std::string FunctionLogPrefix = "fun";
const std::string GlobalVariableLogPrefix = "gv";
const std::string GlobalAliasLogPrefix = "ga";
const std::string StructOffsetLogPrefix = "struct";
const std::string FunctionArgLogPrefix = "fun_arg";

/// A struct type and the offset of an element in that struct
using StructOffset = std::pair<const llvm::StructType *, unsigned>;

/// Set `nosanitize` metadata on an instruction
void setNoSanitizeMetadata(llvm::Instruction *);

/// Print the statistic using AFL's `OKF`
void printStatistic(const llvm::Module &, const llvm::Statistic &);

/// Like `GetUnderlyingObject` in ValueTracking analysis, except that it looks
/// through load instructions
llvm::Value *GetUnderlyingObjectThroughLoads(llvm::Value *,
                                             const llvm::DataLayout &,
                                             unsigned = 6);

/// Get the offset of the struct element at the given byte offset
///
/// This function will recurse through sub-structs, so the returned struct type
/// may be different from the given struct type.
llvm::Optional<StructOffset> getStructOffset(const llvm::StructType *, unsigned,
                                             const llvm::DataLayout &);

#endif // FUZZALLOC_COMMON_H
