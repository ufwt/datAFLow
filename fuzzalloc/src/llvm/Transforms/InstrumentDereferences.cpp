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

static cl::opt<bool> ClInstrumentLoads("instrument-loads",
                                       cl::desc("Instrument loads"));
static cl::opt<bool> ClInstrumentStores("instrument-stores",
                                        cl::desc("Instrument stores"));

STATISTIC(NumOfInstrumentedDereferences,
          "Number of pointer dereferences instrumented.");

namespace {

static constexpr char *const InstrumentationName = "__ptr_deref";

class InstrumentDereference : public ModulePass {
private:
  IntegerType *Int64Ty;
  IntegerType *TagTy;

  ConstantInt *TagShiftSize;
  ConstantInt *TagMask;

  Function *InstrumentationF;

  void doInstrumentDeref(Instruction *, Value *);

public:
  static char ID;
  InstrumentDereference() : ModulePass(ID) {}

  bool doInitialization(Module &) override;
  void getAnalysisUsage(AnalysisUsage &) const override;
  bool runOnModule(Module &) override;
};

} // end anonymous namespace

char InstrumentDereference::ID = 0;

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkInstrumentationFunc(Constant *FuncOrBitcast) {
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

/// Instrument the Instruction `I` that dereferences `Pointer`.
void InstrumentDereference::doInstrumentDeref(Instruction *I, Value *Pointer) {
  IRBuilder<> IRB(I);

  auto *PtrAsInt = IRB.CreatePtrToInt(Pointer, this->Int64Ty);
  auto *PoolId = IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, this->TagShiftSize),
                               this->TagMask);
  auto *PoolIdCast = IRB.CreateIntCast(PoolId, this->TagTy, false);
  auto *InstCall = IRB.CreateCall(this->InstrumentationF, PoolIdCast);

  NumOfInstrumentedDereferences++;
}

void InstrumentDereference::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool InstrumentDereference::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  IntegerType *SizeTTy = DL.getIntPtrType(C);

  this->Int64Ty = Type::getInt64Ty(C);
  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  this->TagShiftSize =
      ConstantInt::get(SizeTTy, NUM_USABLE_BITS - NUM_TAG_BITS);
  this->TagMask = ConstantInt::get(this->TagTy, (1 << NUM_TAG_BITS) - 1);

  return false;
}

bool InstrumentDereference::runOnModule(Module &M) {
  assert(ClInstrumentLoads ||
         ClInstrumentStores && "Must instrument either loads or stores");

  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  Type *VoidTy = Type::getVoidTy(C);

  this->InstrumentationF = checkInstrumentationFunc(
      M.getOrInsertFunction(InstrumentationName, VoidTy, this->TagTy));

  // For determining whether to instrument a memory dereference
  ObjectSizeOpts ObjSizeOpts;
  ObjSizeOpts.RoundToAlign = true;
  ObjectSizeOffsetVisitor ObjSizeVis(DL, TLI, C, ObjSizeOpts);

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      // For every load and/or store instruction (i.e., memory access):
      //
      // 1. Cast the address to an integer
      // 2. Right-shift and mask the address to get the allocation pool ID
      // 3. Call the instrumentation function, passing the pool ID as an
      //    argument
      if (auto *Load = dyn_cast<LoadInst>(&*I)) {
        if (ClInstrumentLoads) {
          auto *Pointer = Load->getPointerOperand();

          // XXX is this check redundant?
          if (!Pointer->getType()->isPointerTy()) {
            continue;
          }

          // Determine whether we should instrument this load instruction
          uint64_t TypeSize = DL.getTypeStoreSizeInBits(Pointer->getType());
          if (!instrumentCheck(ObjSizeVis, Pointer, TypeSize)) {
            continue;
          }

          doInstrumentDeref(Load, Pointer);
        }
      } else if (auto *Store = dyn_cast<StoreInst>(&*I)) {
        if (ClInstrumentStores) {
          auto *Pointer = Store->getPointerOperand();

          // XXX is this check redundant?
          if (!Pointer->getType()->isPointerTy()) {
            continue;
          }

          // Determine whether we should instrument this load instruction
          uint64_t TypeSize = DL.getTypeStoreSizeInBits(Pointer->getType());
          if (!instrumentCheck(ObjSizeVis, Pointer, TypeSize)) {
            continue;
          }

          doInstrumentDeref(Store, Pointer);
        }
      }
    }
  }

  return true;
}

static RegisterPass<InstrumentDereference>
    X("instrument-deref",
      "Instrument pointer dereferences to find their allocation site", false,
      false);
