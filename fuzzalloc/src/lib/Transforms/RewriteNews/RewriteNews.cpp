//===-- RewriteNews.cpp - Rewrite news to mallocs -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass tags rewrites calls to the `new` operator and replaces them with
/// calls to `malloc` so that they can later be tagged. The objects are
/// initialized via `placement new`.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "Common.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-rewrite-news"

STATISTIC(NumOfNewRewrites, "Number of news rewritten.");
STATISTIC(NumOfDeleteRewrites, "Number of deletes rewritten.");

namespace {

/// RewriteNews: Rewrites calls to the `new` operator and replaces them with
/// calls to `malloc`. Objects are initialized via `placement new`.
class RewriteNews : public ModulePass {

public:
  static char ID;
  RewriteNews() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

char RewriteNews::ID = 0;

static bool isNewFn(const Value *V, const TargetLibraryInfo *TLI) {
  if (!isa<Function>(V)) {
    return false;
  }

  const Function *Callee = cast<Function>(V);
  StringRef FnName = Callee->getName();
  LibFunc TLIFn;
  if (!TLI || !TLI->getLibFunc(FnName, TLIFn) || !TLI->has(TLIFn)) {
    return false;
  }

  if (TLIFn == LibFunc_Znwj || TLIFn == LibFunc_ZnwjRKSt9nothrow_t ||
      TLIFn == LibFunc_Znwm || TLIFn == LibFunc_ZnwmRKSt9nothrow_t ||
      TLIFn == LibFunc_Znaj || TLIFn == LibFunc_ZnajRKSt9nothrow_t ||
      TLIFn == LibFunc_Znam || TLIFn == LibFunc_ZnamRKSt9nothrow_t) {
    return true;
  }

  return false;
}

static bool isDeleteFn(const Value *V, const TargetLibraryInfo *TLI) {
  if (!isa<Function>(V)) {
    return false;
  }

  const Function *Callee = cast<Function>(V);
  StringRef FnName = Callee->getName();
  LibFunc TLIFn;
  if (!TLI || !TLI->getLibFunc(FnName, TLIFn) || !TLI->has(TLIFn)) {
    return false;
  }

  if (TLIFn == LibFunc_ZdlPv || TLIFn == LibFunc_ZdaPv) {
    return true;
  }

  return false;
}

static Instruction *rewriteNew(CallSite &CS) {
  LLVM_DEBUG(dbgs() << "rewriting new call " << *CS.getInstruction() << '\n');

  Value *AllocSize = CS.getArgOperand(0);
  Instruction *CSInst = CS.getInstruction();

  auto *MallocCall = CallInst::CreateMalloc(
      CSInst, AllocSize->getType(), CS.getType()->getPointerElementType(),
      AllocSize, nullptr, nullptr, "rewrite_new");

  // If new was invoke-d, rather than call-ed, we must branch to the invoke's
  // normal destination.
  //
  // TODO Emulate exception handling (i.e., the invoke's unwind destination)
  if (auto *Invoke = dyn_cast<InvokeInst>(CSInst)) {
    auto *NormalDest = Invoke->getNormalDest();
    assert(NormalDest && "Invoke has no normal destination");

    BranchInst::Create(NormalDest, CSInst);
  }

  CS->replaceAllUsesWith(MallocCall);
  CS->eraseFromParent();

  NumOfNewRewrites++;

  return MallocCall;
}

static Instruction *rewriteDelete(CallSite &CS) {
  LLVM_DEBUG(dbgs() << "rewriting delete call " << *CS.getInstruction()
                    << '\n');

  Value *Ptr = CS.getArgOperand(CS.getNumArgOperands() - 1);

  auto *FreeCall = CallInst::CreateFree(Ptr, CS.getInstruction());
  CS->replaceAllUsesWith(FreeCall);
  CS->eraseFromParent();

  NumOfDeleteRewrites++;

  return FreeCall;
}

void RewriteNews::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool RewriteNews::runOnModule(Module &M) {
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  // new calls to rewrite
  SmallVector<CallSite, 8> NewCalls;

  // delete calls to rewrite
  SmallVector<CallSite, 8> DeleteCalls;

  for (auto &F : M.functions()) {
    NewCalls.clear();
    DeleteCalls.clear();

    // Collect all the things!
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (isa<CallInst>(&*I) || isa<InvokeInst>(&*I)) {
        CallSite CS(&*I);
        Value *Callee = CS.getCalledValue();

        if (isNewFn(Callee, TLI)) {
          NewCalls.push_back(CS);
        } else if (isDeleteFn(Callee, TLI)) {
          DeleteCalls.push_back(CS);
        }
      }
    }

    // Rewrite new calls
    for (auto &NewCall : NewCalls) {
      rewriteNew(NewCall);
    }

    // Rewrite delete calls
    for (auto &DeleteCall : DeleteCalls) {
      rewriteDelete(DeleteCall);
    }
  }

  // Finished!

  printStatistic(M, NumOfNewRewrites);
  printStatistic(M, NumOfDeleteRewrites);

  return NewCalls.size() > 0 || DeleteCalls.size() > 0;
}

static RegisterPass<RewriteNews>
    X("fuzzalloc-rewrite-news",
      "Replace news with mallocs so that they can be tagged by a later pass",
      false, false);

static void registerRewriteNewsPass(const PassManagerBuilder &,
                                    legacy::PassManagerBase &PM) {
  PM.add(new RewriteNews());
}

static RegisterStandardPasses
    RegisterRewriteNewsPass(PassManagerBuilder::EP_OptimizerLast,
                            registerRewriteNewsPass);

static RegisterStandardPasses
    RegisterRewriteNewsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                             registerRewriteNewsPass);
