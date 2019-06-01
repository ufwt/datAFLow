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

#include "WPA/WPAPass.h" // from SVF

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-svf-analysis"

namespace {

class SVFAnalysis : public ModulePass {
  using AliasResults = std::set<std::pair<const Value *, const Value *>>;
  using ValueSet = SmallPtrSet<const Value *, 24>;

private:
  AliasResults Aliases;

  ValueSet collectTaggedAllocs(Module &M) const;
  ValueSet collectInstrumentedDereferences(Module &M) const;

public:
  static char ID;
  SVFAnalysis() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  void print(llvm::raw_ostream &, const Module *) const override;
  bool runOnModule(Module &) override;
};

} // end anonymous namespace

char SVFAnalysis::ID = 0;

SVFAnalysis::ValueSet SVFAnalysis::collectTaggedAllocs(Module &M) const {
  SVFAnalysis::ValueSet TaggedAllocs;

  for (auto &F : M) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (I->getMetadata(M.getMDKindID("fuzzalloc.tagged_alloc"))) {
        assert(isa<CallInst>(&*I) &&
               "Tagged allocations must be call instructions");
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
        if (auto *Load = dyn_cast<LoadInst>(&*I)) {
          InstrumentedDerefs.insert(Load->getPointerOperand());
        } else if (auto *Store = dyn_cast<StoreInst>(&*I)) {
          InstrumentedDerefs.insert(Store->getPointerOperand());
        } else if (auto *RMW = dyn_cast<AtomicRMWInst>(&*I)) {
          InstrumentedDerefs.insert(RMW->getPointerOperand());
        } else if (auto *XCHG = dyn_cast<AtomicCmpXchgInst>(&*I)) {
          InstrumentedDerefs.insert(XCHG->getPointerOperand());
        } else {
          assert(false && "Unsupported instruction");
        }
      }
    }
  }

  return InstrumentedDerefs;
}

void SVFAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<WPAPass>();
  AU.setPreservesAll();
}

void SVFAnalysis::print(raw_ostream &O, const Module *M) const {
  for (auto AliasPair : this->Aliases) {
    auto *Alloc = cast<CallInst>(AliasPair.first);
    auto *Deref = AliasPair.second;

    // The first argument to a tagged allocation routine is always the
    // allocation site tag
    uint64_t AllocSiteTag =
        cast<ConstantInt>(Alloc->getArgOperand(0))->getZExtValue();

    O << "    allocation site 0x";
    O.write_hex(AllocSiteTag);
    O << " accessed by ";
    Deref->print(O);
    O << "\n";
  }
}

bool SVFAnalysis::runOnModule(Module &M) {
  auto TaggedAllocs = collectTaggedAllocs(M);
  auto InstrumentedDerefs = collectInstrumentedDereferences(M);

  auto &WPAAnalysis = getAnalysis<WPAPass>();

  for (auto TaggedAlloc : TaggedAllocs) {
    for (auto InstrumentedDeref : InstrumentedDerefs) {
      if (WPAAnalysis.alias(TaggedAlloc, InstrumentedDeref)) {
        this->Aliases.insert(std::make_pair(TaggedAlloc, InstrumentedDeref));
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
