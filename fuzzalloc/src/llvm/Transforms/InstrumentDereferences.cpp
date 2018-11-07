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
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
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

static const char *const InstrumentationName = "__ptr_deref";

class InstrumentDereference : public ModulePass {
public:
  static char ID;
  InstrumentDereference() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char InstrumentDereference::ID = 0;

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkInstrumentationFunction(Constant *FuncOrBitcast) {
  if (isa<Function>(FuncOrBitcast)) {
    return cast<Function>(FuncOrBitcast);
  }

  FuncOrBitcast->print(errs());
  errs() << '\n';
  std::string Err;
  raw_string_ostream Stream(Err);
  Stream << "Instrumentation function redefined: " << *FuncOrBitcast;
  report_fatal_error(Err);
}

// Adapted from llvm::AddressSanitizer::isSafeAccess
static bool instrumentCheck(ObjectSizeOffsetVisitor &ObjSizeVis, Value *Addr,
                            uint64_t TypeSize) {
  SizeOffsetType SizeOffset = ObjSizeVis.compute(Addr);
  if (!ObjSizeVis.bothKnown(SizeOffset)) {
    return true;
  }

  uint64_t Size = SizeOffset.first.getZExtValue();
  int64_t Offset = SizeOffset.second.getSExtValue();

  return Offset < 0 || Size < uint64_t(Offset) ||
         Size - uint64_t(Offset) < TypeSize / CHAR_BIT;
}

void InstrumentDereference::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool InstrumentDereference::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  Type *VoidTy = Type::getVoidTy(C);
  IntegerType *Int64Ty = Type::getInt64Ty(C);
  IntegerType *TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  IntegerType *SizeTTy = DL.getIntPtrType(C);

  ConstantInt *TagShiftSize =
      ConstantInt::get(SizeTTy, NUM_USABLE_BITS - NUM_TAG_BITS);
  ConstantInt *TagMask = ConstantInt::get(TagTy, (1 << NUM_TAG_BITS) - 1);

  Function *InstrumentationF = checkInstrumentationFunction(
      M.getOrInsertFunction(InstrumentationName, VoidTy, TagTy));

  // For determining whether to instrument a memory dereference
  ObjectSizeOpts ObjSizeOpts;
  ObjSizeOpts.RoundToAlign = true;
  ObjectSizeOffsetVisitor ObjSizeVis(DL, TLI, C, ObjSizeOpts);

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Load = dyn_cast<LoadInst>(&*I)) {
        // For every load instruction (i.e., memory access):
        //
        // 1. Cast the address to an integer
        // 2. Right-shift and mask the address to get the allocation pool ID
        // 3. Call the instrumentation function, passing the pool ID as an
        //    argument

        auto *Pointer = Load->getPointerOperand();

        // XXX is this check redundant?
        if (!Pointer->getType()->isPointerTy()) {
          continue;
        }

        // Determine whether we should instrument this load instruction
        uint64_t TypeSize = DL.getTypeStoreSizeInBits(Load->getType());
        if (!instrumentCheck(ObjSizeVis, Pointer, TypeSize)) {
          continue;
        }

        IRBuilder<> IRB(Load);

        auto *PtrAsInt = IRB.CreatePtrToInt(Pointer, Int64Ty);
        auto *PoolId =
            IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, TagShiftSize), TagMask);
        auto *PoolIdCast = IRB.CreateIntCast(PoolId, TagTy, false);
        auto *InstCall = IRB.CreateCall(InstrumentationF, PoolIdCast);

        NumOfInstrumentedDereferences++;
      }
    }
  }

  return true;
}

static RegisterPass<InstrumentDereference>
    X("instrument-deref",
      "Instrument pointer dereferences to find their allocation site", false,
      false);