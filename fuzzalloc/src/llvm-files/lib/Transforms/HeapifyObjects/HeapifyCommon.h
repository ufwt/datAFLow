//===-- HeapifyCommon.h - Heapify static arrays ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality for static array/struct heapification.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_HEAPIFY_COMMON_H
#define FUZZALLOC_HEAPIFY_COMMON_H

#include "llvm/IR/IRBuilder.h"

namespace llvm {
class DataLayout;
class GetElementPtrInst;
class Instruction;
class LLVMContext;
class Type;
class Twine;
class Value;
} // namespace llvm

/// Priority for heapified global variable constructor
const unsigned kHeapifyGVCtorAndDtorPriority = 0;

/// Update a GEP instruction to use the given value
llvm::Value *updateGEP(llvm::GetElementPtrInst *, llvm::Value *);

/// Returns \c true if the given type is heapifiable to dynamic allocation.
bool isHeapifiableType(llvm::Type *);

/// Returns \c true if the given value is C++ virtual table or type info
/// metadata
bool isVTableOrTypeInfo(const llvm::Value *);

/// Create a call to \c malloc that will create an array.
llvm::Instruction *createArrayMalloc(llvm::LLVMContext &,
                                     const llvm::DataLayout &,
                                     llvm::IRBuilder<> &, llvm::Type *,
                                     uint64_t, const llvm::Twine & = "");

/// Insert a call to \c free for the given alloca
void insertFree(llvm::Value *, llvm::Instruction *);

#endif // FUZZALLOC_HEAPIFY_COMMON_H
