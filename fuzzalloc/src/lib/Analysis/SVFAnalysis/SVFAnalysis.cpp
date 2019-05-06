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

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

// fuzzalloc includes
#include "llvm/Transforms/InstrumentDereferences.h"
#include "llvm/Transforms/TagDynamicAllocs.h"

// SVF includes
#include "WPA/WPAPass.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-svf-analysis"

namespace {

class SVFAnalysis : public ModulePass {
  using AliasResults = std::set<std::pair<const Value *, const Value *>>;

private:
  bool runOnModule(SVFModule);

public:
  static char ID;
  SVFAnalysis() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool runOnModule(Module &) override;
};

} // end anonymous namespace

char SVFAnalysis::ID = 0;

void SVFAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<InstrumentDereferences>();
  AU.addRequired<TagDynamicAllocs>();
  AU.addRequired<WPAPass>();

  AU.setPreservesAll();
}

bool SVFAnalysis::runOnModule(SVFModule M) {
  auto TaggedAllocs = getAnalysis<TagDynamicAllocs>().getTaggedAllocs();
  auto InstrumentedDerefs =
      getAnalysis<InstrumentDereferences>().getInstrumentedDereferences();

  AliasResults Aliases;

  for (auto TaggedAlloc : TaggedAllocs) {
    for (auto InstrumentedDeref : InstrumentedDerefs) {
      if (getAnalysis<WPAPass>().alias(TaggedAlloc, InstrumentedDeref)) {
        Aliases.insert(std::make_pair(TaggedAlloc, InstrumentedDeref));
      }
    }
  }

  return false;
}

bool SVFAnalysis::runOnModule(Module &M) { return runOnModule(SVFModule(M)); }

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
