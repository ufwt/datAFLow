//===-- InstrumentDereferences.cpp - Instrument pointer dereferences ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass instruments pointer dereferences (i.e., \p load instructions) to
/// discover their allocation site.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "instrument-deref"

STATISTIC(NumOfInstrumentedDereferences,
          "Number of pointer dereferences instrumented.");

namespace {

class InstrumentDereference : public ModulePass {
public:
  static char ID;
  InstrumentDereference() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char InstrumentDereference::ID = 0;

bool InstrumentDereference::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  IntegerType *Int64Ty = Type::getInt64Ty(C);
  IntegerType *SizeTTy = DL.getIntPtrType(C);

  ConstantInt *TagShiftSize =
      ConstantInt::get(SizeTTy, NUM_USABLE_BITS - NUM_TAG_BITS);
  ConstantInt *TagMask = ConstantInt::get(SizeTTy, (1 << NUM_TAG_BITS) - 1);

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Load = dyn_cast<LoadInst>(&*I)) {
        auto *Pointer = Load->getPointerOperand();

        // XXX is this check redundant?
        if (!Pointer->getType()->isPointerTy()) {
          continue;
        }

        IRBuilder<> IRB(Load);

        auto *PtrAsInt = IRB.CreatePtrToInt(Pointer, Int64Ty);
        auto *PoolId =
            IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, TagShiftSize), TagMask);
      }
    }
  }

  return true;
}

static RegisterPass<InstrumentDereference>
    X("instrument-deref",
      "Instrument pointer dereferences to find their allocation site", false,
      false);
