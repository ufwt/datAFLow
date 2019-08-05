//===-- ExpandGVInitializers.cpp - Expand global variable initializers ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Expand global variables with constant initializers.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "Common.h"
#include "PromoteCommon.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-expand-gv-initializers"

STATISTIC(NumOfExpandedGlobalVariables,
          "Number of expanded global variable initializers");

namespace {

/// ExpandGVInitializers: rewrite global variable static initializers to
/// dynamic initializers in the module's constructor.
class ExpandGVInitializers : public ModulePass {
private:
  SmallPtrSet<Constant *, 8> DeadConstants;

  Function *expandInitializer(GlobalVariable *);

public:
  static char ID;
  ExpandGVInitializers() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char ExpandGVInitializers::ID = 0;

static bool constantStructContainsArray(const ConstantStruct *ConstStruct) {
  for (auto &Op : ConstStruct->operands()) {
    if (isa<ArrayType>(Op->getType())) {
      return true;
    } else if (auto *GEP = dyn_cast<GEPOperator>(Op)) {
      if (isa<ArrayType>(GEP->getSourceElementType())) {
        return true;
      }
    } else if (auto *StructOp = dyn_cast<ConstantStruct>(Op)) {
      return constantStructContainsArray(StructOp);
    }
  }

  return false;
}

/// Recursively expand `ConstantAggregate`s by generating equivalent
/// instructions in a module constructor.
static void expandConstantAggregate(IRBuilder<> &IRB, GlobalVariable *GV,
                                    ConstantAggregate *CA,
                                    std::vector<unsigned> &Idxs) {
  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();
  IntegerType *Int32Ty = Type::getInt32Ty(C);

  // Converts an unsigned integer to something IRBuilder understands
  auto UnsignedToInt32 = [Int32Ty](const unsigned &N) {
    return ConstantInt::get(Int32Ty, N);
  };

  for (unsigned I = 0; I < CA->getNumOperands(); ++I) {
    auto *Op = CA->getOperand(I);

    if (auto *AggOp = dyn_cast<ConstantAggregate>(Op)) {
      // Expand the nested ConstantAggregate
      Idxs.push_back(I);
      expandConstantAggregate(IRB, GV, AggOp, Idxs);
      Idxs.pop_back();
    } else {
      std::vector<Value *> IdxValues(Idxs.size());
      std::transform(Idxs.begin(), Idxs.end(), IdxValues.begin(),
                     UnsignedToInt32);
      IdxValues.push_back(UnsignedToInt32(I));

      auto *Store = IRB.CreateStore(Op, IRB.CreateInBoundsGEP(GV, IdxValues));
      Store->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                         MDNode::get(C, None));
    }
  }
}

/// Move global variable's who have a `ConstantAggregate` initializer into a
/// constructor function.
Function *ExpandGVInitializers::expandInitializer(GlobalVariable *GV) {
  LLVM_DEBUG(dbgs() << "expanding initializer for global variable " << *GV
                    << '\n');

  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();
  Constant *Initializer = GV->getInitializer();

  // Create the constructor
  //
  // The constructor must be executed after the promoted global variable's
  // constructor, hence the higher priority
  FunctionType *GlobalCtorTy =
      FunctionType::get(Type::getVoidTy(C), /* isVarArg */ false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "fuzzalloc.init_" + GV->getName(), M);
  appendToGlobalCtors(*M, GlobalCtorF, kPromotedGVCtorAndDtorPriority + 1);

  BasicBlock *GlobalCtorBB = BasicBlock::Create(C, "", GlobalCtorF);

  IRBuilder<> IRB(GlobalCtorBB);
  for (unsigned I = 0; I < Initializer->getNumOperands(); ++I) {
    auto *Op = Initializer->getOperand(I);

    if (auto *AggregateOp = dyn_cast<ConstantAggregate>(Op)) {
      std::vector<unsigned> Idxs = {0, I};
      expandConstantAggregate(IRB, GV, AggregateOp, Idxs);
    } else {
      auto *Store = IRB.CreateStore(
          Op, IRB.CreateConstInBoundsGEP2_32(nullptr, GV, 0, I));
      Store->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                         MDNode::get(C, None));
    }
  }
  IRB.CreateRetVoid();

  this->DeadConstants.insert(Initializer);
  GV->setInitializer(ConstantAggregateZero::get(GV->getValueType()));

  NumOfExpandedGlobalVariables++;

  return GlobalCtorF;
}

bool ExpandGVInitializers::runOnModule(Module &M) {
  for (auto &GV : M.globals()) {
    if (GV.getName().startswith("llvm.")) {
      continue;
    }

    if (!GV.hasInitializer()) {
      continue;
    }

    // XXX Check for private or internal linkage
    if (GV.isConstant()) {
      continue;
    }

    if (isVTableOrTypeInfo(&GV)) {
      continue;
    }

    const Constant *Initializer = GV.getInitializer();
    if (!isa<ConstantAggregate>(Initializer)) {
      continue;
    }

    if (auto *ConstArray = dyn_cast<ConstantArray>(Initializer)) {
      if (isPromotableType(ConstArray->getType())) {
        expandInitializer(&GV);
      }
    } else if (auto *ConstStruct = dyn_cast<ConstantStruct>(Initializer)) {
      if (constantStructContainsArray(ConstStruct)) {
        expandInitializer(&GV);
      }
    } else if (isa<ConstantVector>(Initializer)) {
      assert(false && "Constant vector initializers not supported");
    }
  }

  for (auto *C : this->DeadConstants) {
    C->destroyConstant();
  }

  printStatistic(M, NumOfExpandedGlobalVariables);

  return NumOfExpandedGlobalVariables > 0;
}

static RegisterPass<ExpandGVInitializers>
    X("fuzzalloc-expand-gv-initializers",
      "Expand global variable static initializers", false, false);

static void registerExpandGVInitializersPass(const PassManagerBuilder &,
                                             legacy::PassManagerBase &PM) {
  PM.add(new ExpandGVInitializers());
}

static RegisterStandardPasses RegisterExpandGVInitializersPass(
    PassManagerBuilder::EP_ModuleOptimizerEarly,
    registerExpandGVInitializersPass);

static RegisterStandardPasses
    RegisterExpandGVInitializersPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                      registerExpandGVInitializersPass);
