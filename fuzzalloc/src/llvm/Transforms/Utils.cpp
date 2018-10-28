//===-- Utils.cpp - fuzzalloc LLVM pass utilities--------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines helper functions for \p fuzzalloc's LLVM passes.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"

#include "Utils.h"

void insertFree(llvm::AllocaInst *Alloca, llvm::ReturnInst *Return) {
  llvm::IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(Alloca);
  llvm::CallInst::CreateFree(LoadMalloc, Return);
}
