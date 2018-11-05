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

using namespace llvm;

void insertFree(Instruction *MallocPtr, ReturnInst *Return) {
  IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(MallocPtr);
  CallInst::CreateFree(LoadMalloc, Return);
}
