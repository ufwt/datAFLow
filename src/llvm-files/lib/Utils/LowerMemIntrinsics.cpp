//===-- LowerMemIntrinics.cpp - Lower llvm.mem* intrinsics ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Wrapper around LLVM's LowerMemIntrinics functionality.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"

#include "LowerMemIntrinsics.h"

using namespace llvm;

#define DEBUG_TYPE "lower-mem-intrinsics"

void LowerMemIntrinsics::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetTransformInfoWrapperPass>();
}

bool LowerMemIntrinsics::runOnFunction(Function &F) {
  bool Changed = false;
  SmallVector<MemIntrinsic *, 8> InstsToDelete;

  for (auto &I : instructions(F)) {
    if (auto *MemCpy = dyn_cast<MemCpyInst>(&I)) {
      const auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
      expandMemCpyAsLoop(MemCpy, TTI);
      InstsToDelete.push_back(MemCpy);
      Changed = true;
    } else if (auto *MemMove = dyn_cast<MemMoveInst>(&I)) {
      expandMemMoveAsLoop(MemMove);
      InstsToDelete.push_back(MemMove);
      Changed = true;
    } else if (auto *MemSet = dyn_cast<MemSetInst>(&I)) {
      expandMemSetAsLoop(MemSet);
      InstsToDelete.push_back(MemSet);
      Changed = true;
    }
  }

  for (auto *MemInst : InstsToDelete) {
    MemInst->eraseFromParent();
  }

  return Changed;
}

char LowerMemIntrinsics::ID = 0;

INITIALIZE_PASS(LowerMemIntrinsics, DEBUG_TYPE, "Lower memory intrinsics",
                false, false)
