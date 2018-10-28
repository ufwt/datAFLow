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
/// This header file defines helper for the custom memory mananger,
/// \p fuzzalloc.
///
//===----------------------------------------------------------------------===//

#ifndef _FUZZ_ALLOC_H_
#define _FUZZ_ALLOC_H_

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"

/// Metadata label for storing the total size (in bytes) of a promoted static
/// array
const char *const ARRAY_PROM_SIZE_MD = "static-array-prom.size";

/// \p free the given allocation before the given return address.
void insertFree(llvm::AllocaInst *Alloca, llvm::ReturnInst *Return) {
  llvm::IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(Alloca);
  llvm::CallInst::CreateFree(LoadMalloc, Return);
}

#endif
