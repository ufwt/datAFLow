//===-- SVFAnalysis.cpp - Static dataflow analysis ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Performs an approximate dataflow analysis using SVF.
///
//===----------------------------------------------------------------------===//

#include <set>

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

// SVF includes
#include "WPA/WPAPass.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-svf-analysis"

namespace {

class SVFAnalysis : public ModulePass {
  using AliasResults = std::set<std::pair<const Value *, const Value *>>;
  using ValueSet = SmallPtrSet<const Value *, 24>;

private:
  ValueSet collectTaggedAllocs(Module &M) const;
  ValueSet collectInstrumentedDereferences(Module &M) const;

public:
  static char ID;
  SVFAnalysis() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool runOnModule(Module &) override;
};

} // end anonymous namespace

char SVFAnalysis::ID = 0;

SVFAnalysis::ValueSet SVFAnalysis::collectTaggedAllocs(Module &M) const {
  SVFAnalysis::ValueSet TaggedAllocs;

  for (auto &F : M) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (I->getMetadata(M.getMDKindID("fuzzalloc.tagged_alloc"))) {
        TaggedAllocs.insert(&*I);
      }
    }
  }

  return TaggedAllocs;
}

SVFAnalysis::ValueSet
SVFAnalysis::collectInstrumentedDereferences(Module &M) const {
  SVFAnalysis::ValueSet InstrumentedDerefs;

  for (auto &F : M) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (I->getMetadata(M.getMDKindID("fuzzalloc.instrumented_deref"))) {
        InstrumentedDerefs.insert(&*I);
      }
    }
  }

  return InstrumentedDerefs;
}

void SVFAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<WPAPass>();
  AU.setPreservesAll();
}

bool SVFAnalysis::runOnModule(Module &M) {
  auto TaggedAllocs = collectTaggedAllocs(M);
  auto InstrumentedDerefs = collectInstrumentedDereferences(M);

  auto &WPAAnalysis = getAnalysis<WPAPass>();
  AliasResults Aliases;

  for (auto TaggedAlloc : TaggedAllocs) {
    for (auto InstrumentedDeref : InstrumentedDerefs) {
      if (WPAAnalysis.alias(TaggedAlloc, InstrumentedDeref)) {
        Aliases.insert(std::make_pair(TaggedAlloc, InstrumentedDeref));
      }
    }
  }

  return false;
}

static RegisterPass<SVFAnalysis> X("fuzzalloc-svf-analysis",
                                   "Static dataflow analysis", false, false);

static void registerSVFAnalysisPass(const PassManagerBuilder &,
                                    legacy::PassManagerBase &PM) {
  PM.add(new SVFAnalysis());
}

static RegisterStandardPasses
    RegisterSVFAnalysisPass(PassManagerBuilder::EP_OptimizerLast,
                            registerSVFAnalysisPass);

static RegisterStandardPasses
    RegisterSVFAnalysisPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                             registerSVFAnalysisPass);
