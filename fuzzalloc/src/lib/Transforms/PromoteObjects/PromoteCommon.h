//===-- PromoteCommon.h - Promote static arrays to mallocs ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality for static array/struct promotion.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_PROMOTE_COMMON_H
#define FUZZALLOC_PROMOTE_COMMON_H

#include "llvm/IR/IRBuilder.h"

namespace llvm {
class DataLayout;
class GetElementPtrInst;
class Instruction;
class LLVMContext;
class ReturnInst;
class SelectInst;
class Type;
class Twine;
class Value;
} // namespace llvm

/// Priority for promoted global variable constructor
const unsigned kPromotedGVCtorAndDtorPriority = 0;

/// Update a GEP instruction to use the given value
llvm::Value *updateGEP(llvm::GetElementPtrInst *, llvm::Value *);

/// Update a select instruction to use the given value
llvm::SelectInst *updateSelect(llvm::SelectInst *, llvm::Value *,
                               llvm::Value *);

/// Update a return instruction to use the given value
llvm::ReturnInst *updateReturn(llvm::ReturnInst *, llvm::Value *,
                               llvm::Value *);

/// Returns \c true if the given type is promotable to dynamic allocation.
bool isPromotableType(llvm::Type *);

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

#endif // FUZZALLOC_PROMOTE_COMMON_H
