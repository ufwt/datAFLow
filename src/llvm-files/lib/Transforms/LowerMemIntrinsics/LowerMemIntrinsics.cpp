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
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "lower-mem-intrinsics"

class LowerMemIntrinsics : public llvm::FunctionPass {
public:
  static char ID;
  LowerMemIntrinsics() : llvm::FunctionPass(ID) {}
  virtual bool runOnFunction(llvm::Function &) override;
  virtual void getAnalysisUsage(llvm::AnalysisUsage &) const override;
};

char LowerMemIntrinsics::ID = 0;

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

//===----------------------------------------------------------------------===//

static RegisterPass<LowerMemIntrinsics>
    X("fuzzalloc-mem-intrinsics", "Lower memory intrinsics", false, false);

static void registerLowerMemIntrinsicsPass(const PassManagerBuilder &,
                                           legacy::PassManagerBase &PM) {
  PM.add(new LowerMemIntrinsics());
}

static RegisterStandardPasses
    RegisterLowerMemIntrinsicsPass(PassManagerBuilder::EP_OptimizerLast,
                                   registerLowerMemIntrinsicsPass);

static RegisterStandardPasses
    RegisterLowerMemIntrinsicsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                    registerLowerMemIntrinsicsPass);
