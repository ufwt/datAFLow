//===-- PromoteCommon.cpp - Promote static arrays to mallocs --------------===//
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

#include "llvm/IR/Instructions.h"

#include "PromoteCommon.h"

using namespace llvm;

void insertFree(AllocaInst *MallocPtr, Instruction *Inst) {
  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = new LoadInst(MallocPtr, "", Inst);
  CallInst::CreateFree(LoadMalloc, Inst);
}
