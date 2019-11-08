//===-- CountObjects.cpp - Counts objects allocated in memory -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass simply counts the number of memory-allocated objects. This
/// includes `alloca`s and global variables.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "Common.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-count-objects"

namespace {

/// Count the number of `alloca`s and global variables
class CountObjects : public ModulePass {
private:
  unsigned long NumOfAllocas;
  unsigned long NumOfGlobalVars;

public:
  static char ID;
  CountObjects() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  void print(llvm::raw_ostream &, const Module *) const override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

char CountObjects::ID = 0;

void CountObjects::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

void CountObjects::print(raw_ostream &O, const Module *M) const {
  O << "  num. allocas: " << this->NumOfAllocas << "\n";
  O << "  num. global variables: " << this->NumOfGlobalVars << "\n";
}

bool CountObjects::runOnModule(Module &M) {
  this->NumOfAllocas = 0;
  this->NumOfGlobalVars = 0;

  for (const auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (isa<AllocaInst>(&*I)) {
        this->NumOfAllocas++;
      }
    }
  }

  for (const auto &G : M.globals()) {
    if (auto *GV = dyn_cast<GlobalVariable>(&G)) {
      if (!GV->isDeclaration()) {
        this->NumOfGlobalVars++;
      }
    }
  }

  return false;
}

static RegisterPass<CountObjects>
    X("fuzzalloc-count-objects",
      "Count the number of allocas and global variables", false, false);

static void registerCountObjectsPass(const PassManagerBuilder &,
                                     legacy::PassManagerBase &PM) {
  PM.add(new CountObjects());
}

static RegisterStandardPasses
    RegisterCountObjectsPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
                             registerCountObjectsPass);

static RegisterStandardPasses
    RegisterCountObjectsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                              registerCountObjectsPass);
