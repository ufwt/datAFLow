//===-- TagpMalloc.cpp - Tag mallocs with a unique identifier -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass tags calls to \p malloc with a randomly-generated identifier
/// and calls fuzzalloc's \p malloc wrapper function.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "tag-malloc"

STATISTIC(NumOfTaggedMalloc, "Number of malloc calls tagged.");

namespace {

class TagMalloc : public ModulePass {
public:
  static char ID;
  TagMalloc() : ModulePass(ID) {}

  bool runOnModule(Module &F) override;
};

} // end anonymous namespace

char TagMalloc::ID = 0;

bool TagMalloc::runOnModule(Module &M) {
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (isAllocLikeFn(&*I, &TLI)) {
      }
    }
  }

  return true;
}

static RegisterPass<TagMalloc>
    X("tag-malloc",
      "Tag malloc calls and replace them with a call to "
      "fuzzalloc's malloc wrapper",
      false, false);
