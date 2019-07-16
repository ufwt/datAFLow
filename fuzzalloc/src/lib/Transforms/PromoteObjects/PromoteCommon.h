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
class Type;
class Twine;
class Value;
} // namespace llvm

/// Update a GEP to use the given value
llvm::Value *updateGEP(llvm::GetElementPtrInst *, llvm::Value *);

/// Returns \c true if the given type is promotable to dynamic allocation.
bool isPromotableType(llvm::Type *);

/// Create a call to \c malloc that will create an array.
llvm::Instruction *createArrayMalloc(llvm::LLVMContext &,
                                     const llvm::DataLayout &,
                                     llvm::IRBuilder<> &, llvm::Type *,
                                     uint64_t, const llvm::Twine & = "");

/// Insert a call to \c free for the given alloca
void insertFree(llvm::Value *, llvm::Instruction *);

#endif // FUZZALLOC_PROMOTE_COMMON_H
