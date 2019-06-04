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

#include <unordered_set>

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/DebugInfoMetadata.h"
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

struct FuzzallocAlias {
  const Value *TaggedAlloc;
  const Value *InstrumentedDeref;
  const AliasResult Result;

  FuzzallocAlias(const Value *TA, const Value *ID, const AliasResult &R)
      : TaggedAlloc(TA), InstrumentedDeref(ID), Result(R) {}
};

class SVFAnalysis : public ModulePass {
  using ValueSet = SmallPtrSet<const Value *, 24>;
  using AliasResults = std::unordered_set<const FuzzallocAlias *>;

private:
  AliasResults Aliases;

  ValueSet collectTaggedAllocs(Module &M) const;
  ValueSet collectInstrumentedDereferences(Module &M) const;

public:
  static char ID;
  SVFAnalysis() : ModulePass(ID) {}
  virtual ~SVFAnalysis();

  void getAnalysisUsage(AnalysisUsage &) const override;
  void print(llvm::raw_ostream &, const Module *) const override;
  bool runOnModule(Module &) override;
};

} // end anonymous namespace

char SVFAnalysis::ID = 0;

SVFAnalysis::~SVFAnalysis() {
  for (auto *Alias : Aliases) {
    delete Alias;
  }
}

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
  for (auto *Alias : this->Aliases) {
    auto *Alloc = cast<CallInst>(Alias->TaggedAlloc);
    auto *Deref = cast<Instruction>(Alias->InstrumentedDeref);
    auto AResult = Alias->Result;

    // The first argument to a tagged allocation routine is always the
    // allocation site tag
    uint64_t AllocSiteTag =
        cast<ConstantInt>(Alloc->getArgOperand(0))->getZExtValue();

    O << "    allocation site 0x";
    O.write_hex(AllocSiteTag);
    if (DILocation *AllocLoc = Alloc->getDebugLoc()) {
      O << " (" << AllocLoc->getFilename() << ":" << AllocLoc->getLine() << ")";
    }
    O << (AResult == MustAlias ? " IS " : " MAY BE ");
    O << "accessed in function ";
    O << Deref->getFunction()->getName();
    if (DILocation *DerefLoc = Deref->getDebugLoc()) {
      O << " (" << DerefLoc->getFilename() << ":" << DerefLoc->getLine() << ")";
    }
    O << "\n";
  }
}

bool SVFAnalysis::runOnModule(Module &M) {
  auto TaggedAllocs = collectTaggedAllocs(M);
  auto InstrumentedDerefs = collectInstrumentedDereferences(M);

  auto &WPAAnalysis = getAnalysis<WPAPass>();

  for (auto TaggedAlloc : TaggedAllocs) {
    for (auto InstrumentedDeref : InstrumentedDerefs) {
      AliasResult Res = WPAAnalysis.alias(TaggedAlloc, InstrumentedDeref);
      if (Res) {
        this->Aliases.emplace(
            new FuzzallocAlias(TaggedAlloc, InstrumentedDeref, Res));
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
