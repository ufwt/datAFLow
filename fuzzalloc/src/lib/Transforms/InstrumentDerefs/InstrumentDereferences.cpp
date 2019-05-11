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

#include <set>

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include "debug.h" // from afl
#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-instrument-derefs"

static cl::opt<bool>
    ClInstrumentWrites("fuzzalloc-instrument-writes",
                       cl::desc("Instrument write instructions"));
static cl::opt<bool>
    ClInstrumentReads("fuzzalloc-instrument-reads",
                      cl::desc("Instrument read instructions"));
static cl::opt<bool> ClInstrumentAtomics(
    "fuzzalloc-instrument-atomics",
    cl::desc("Instrument atomic instructions (rmw, cmpxchg)"));

STATISTIC(NumOfInstrumentedDereferences,
          "Number of pointer dereferences instrumented.");

static const char *const InstrumentationName = "__ptr_deref";

namespace {

class InstrumentDereferences : public ModulePass {
private:
  IntegerType *Int64Ty;
  IntegerType *TagTy;

  ConstantInt *TagShiftSize;
  ConstantInt *TagMask;

  void doInstrumentDeref(Instruction *, Value *, Function *);

public:
  static char ID;
  InstrumentDereferences() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

char InstrumentDereferences::ID = 0;

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
static bool isSafeAccess(ObjectSizeOffsetVisitor &ObjSizeVis, Value *Addr,
                         uint64_t TypeSize) {
  SizeOffsetType SizeOffset = ObjSizeVis.compute(Addr);
  if (!ObjSizeVis.bothKnown(SizeOffset)) {
    return false;
  }

  uint64_t Size = SizeOffset.first.getZExtValue();
  int64_t Offset = SizeOffset.second.getSExtValue();

  // Three checks are required to ensure safety:
  // - Offset >= 0 (since the offset is given from the base ptr)
  // - Size >= Offset (unsigned)
  // - Size - Offset >= NeededSize (unsigned)
  return Offset >= 0 && Size >= uint64_t(Offset) &&
         Size - uint64_t(Offset) >= TypeSize / CHAR_BIT;
}

// Adapted from llvm::AddressSanitizer::getAllocaSizeInBytes
static uint64_t getAllocaSizeInBytes(const AllocaInst &AI) {
  uint64_t ArraySize = 1;

  if (AI.isArrayAllocation()) {
    const ConstantInt *CI = dyn_cast<ConstantInt>(AI.getArraySize());
    assert(CI && "Non-constant array size");
    ArraySize = CI->getZExtValue();
  }

  Type *Ty = AI.getAllocatedType();
  uint64_t SizeInBytes = AI.getModule()->getDataLayout().getTypeAllocSize(Ty);

  return SizeInBytes * ArraySize;
}

// Adapted from llvm::AddressSanitizer::isInterestingAlloca
static bool isInterestingAlloca(const AllocaInst &AI) {
  return AI.getAllocatedType()->isSized() &&
         // alloca() may be called with 0 size, ignore it
         ((!AI.isStaticAlloca()) || getAllocaSizeInBytes(AI) > 0) &&
         // We are only interested in allocas not promotable to registers
         !isAllocaPromotable(&AI) &&
         // inalloca allocas are not treated as static, and we don't want
         // dynamic alloca instrumentation for them also
         !AI.isUsedWithInAlloca() &&
         // swifterror allocas are register promoted by ISel
         !AI.isSwiftError();
}

// Adapted from llvm::AddressSanitizer::isInterestingMemoryAccess
static Value *isInterestingMemoryAccess(Instruction *I, bool *IsWrite,
                                        uint64_t *TypeSize, unsigned *Alignment,
                                        Value **MaybeMask = nullptr) {
  Value *PtrOperand = nullptr;
  const DataLayout &DL = I->getModule()->getDataLayout();

  if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (!ClInstrumentReads) {
      return nullptr;
    }

    *IsWrite = false;
    *TypeSize = DL.getTypeStoreSizeInBits(LI->getType());
    *Alignment = LI->getAlignment();
    PtrOperand = LI->getPointerOperand();
  } else if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (!ClInstrumentWrites) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(SI->getValueOperand()->getType());
    *Alignment = SI->getAlignment();
    PtrOperand = SI->getPointerOperand();
  } else if (auto *RMW = dyn_cast<AtomicRMWInst>(I)) {
    if (!ClInstrumentAtomics) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(RMW->getValOperand()->getType());
    *Alignment = 0;
    PtrOperand = RMW->getPointerOperand();
  } else if (auto *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (!ClInstrumentAtomics) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize = DL.getTypeStoreSizeInBits(XCHG->getCompareOperand()->getType());
    *Alignment = 0;
    PtrOperand = XCHG->getPointerOperand();
  } else if (auto *CI = dyn_cast<CallInst>(I)) {
    auto *F = dyn_cast<Function>(CI->getCalledValue());
    if (F && (F->getName().startswith("llvm.masked.load.") ||
              F->getName().startswith("llvm.masked.store."))) {
      unsigned OpOffset = 0;

      if (F->getName().startswith("llvm.masked.store.")) {
        if (!ClInstrumentWrites) {
          return nullptr;
        }

        // Masked store has an initial operand for the value
        OpOffset = 1;
        *IsWrite = true;
      } else {
        if (!ClInstrumentReads) {
          return nullptr;
        }

        *IsWrite = false;
      }

      auto *BasePtr = CI->getOperand(0 + OpOffset);
      auto *Ty = cast<PointerType>(BasePtr->getType())->getElementType();
      *TypeSize = DL.getTypeStoreSizeInBits(Ty);

      if (auto *AlignmentConstant =
              dyn_cast<ConstantInt>(CI->getOperand(1 + OpOffset))) {
        *Alignment = (unsigned)AlignmentConstant->getZExtValue();
      } else {
        *Alignment = 1; // No alignment guarantees
      }

      if (MaybeMask) {
        *MaybeMask = CI->getOperand(2 + OpOffset);
        PtrOperand = BasePtr;
      }
    }
  }

  if (PtrOperand) {
    // Do not instrument accesses from different address spaces; we cannot
    // deal with them
    Type *PtrTy = cast<PointerType>(PtrOperand->getType()->getScalarType());
    if (PtrTy->getPointerAddressSpace() != 0) {
      return nullptr;
    }

    // Ignore swifterror addresses
    if (PtrOperand->isSwiftError()) {
      return nullptr;
    }
  }

  // Treat memory accesses to promotable allocas as non-interesting since they
  // will not cause memory violations
  if (auto *AI = dyn_cast_or_null<AllocaInst>(PtrOperand)) {
    return isInterestingAlloca(*AI) ? AI : nullptr;
  }

  return PtrOperand;
}

/// Instrument the Instruction `I` that dereferences `Pointer`.
void InstrumentDereferences::doInstrumentDeref(Instruction *I, Value *Pointer,
                                               Function *InstrumentationF) {
  IRBuilder<> IRB(I);
  auto *M = I->getModule();

  // This metadata can be used by the static pointer analysis
  I->setMetadata(M->getMDKindID("fuzzalloc.instrumented_deref"),
                 MDNode::get(IRB.getContext(), None));

  auto *PtrAsInt = IRB.CreatePtrToInt(Pointer, this->Int64Ty);
  if (auto PtrAsIntInst = dyn_cast<Instruction>(PtrAsInt)) {
    PtrAsIntInst->setMetadata(M->getMDKindID("nosanitize"),
                              MDNode::get(IRB.getContext(), None));
  }

  auto *PoolId = IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, this->TagShiftSize),
                               this->TagMask);
  auto *PoolIdCast = IRB.CreateIntCast(PoolId, this->TagTy, false);
  IRB.CreateCall(InstrumentationF, PoolIdCast);

  NumOfInstrumentedDereferences++;
}

void InstrumentDereferences::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool InstrumentDereferences::doInitialization(Module &M) {
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

bool InstrumentDereferences::runOnModule(Module &M) {
  assert((ClInstrumentReads || ClInstrumentWrites) &&
         "Must instrument either loads or stores");

  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  Type *VoidTy = Type::getVoidTy(C);

  Function *InstrumentationF = checkInstrumentationFunc(
      M.getOrInsertFunction(InstrumentationName, VoidTy, this->TagTy));

  // For determining whether to instrument a memory dereference
  ObjectSizeOpts ObjSizeOpts;
  ObjSizeOpts.RoundToAlign = true;
  ObjectSizeOffsetVisitor ObjSizeVis(DL, TLI, C, ObjSizeOpts);

  for (auto &F : M.functions()) {
    // We only want to instrument every address only once per basic block
    // (unless there are calls between uses)
    SmallPtrSet<Value *, 16> TempsToInstrument;
    SmallVector<Instruction *, 16> ToInstrument;
    bool IsWrite;
    unsigned Alignment;
    uint64_t TypeSize;

    for (auto &BB : F) {
      TempsToInstrument.clear();

      for (auto &Inst : BB) {
        Value *MaybeMask = nullptr;

        if (Value *Addr = isInterestingMemoryAccess(&Inst, &IsWrite, &TypeSize,
                                                    &Alignment, &MaybeMask)) {
          // If we have a mask, skip instrumentation if we've already
          // instrumented the full object. But don't add to TempsToInstrument
          // because we might get another load/store with a different mask
          if (MaybeMask) {
            if (TempsToInstrument.count(Addr)) {
              // We've seen this (whole) temp in the current BB
              continue;
            }
          } else {
            if (!TempsToInstrument.insert(Addr).second) {
              // We've seen this temp in the current BB
              continue;
            }
          }
        }
        // TODO pointer comparisons?
        else if (isa<MemIntrinsic>(Inst)) {
          // ok, take it.
        } else {
          CallSite CS(&Inst);

          if (CS) {
            // A call inside BB
            TempsToInstrument.clear();
          }

          continue;
        }

        // Finally, check if the instruction has the "no_instrument" metadata
        // attached to it (from the array/struct promotion pass)
        if (!Inst.getMetadata(M.getMDKindID("fuzzalloc.no_instrument"))) {
          ToInstrument.push_back(&Inst);
        }
      }
    }

    // Instrument memory operations
    for (auto *I : ToInstrument) {
      if (Value *Addr =
              isInterestingMemoryAccess(I, &IsWrite, &TypeSize, &Alignment)) {
        // A direct inbounds access to a stack variable is always valid
        if (isa<AllocaInst>(GetUnderlyingObject(Addr, DL)) &&
            isSafeAccess(ObjSizeVis, Addr, TypeSize)) {
          continue;
        }

        doInstrumentDeref(I, Addr, InstrumentationF);
      } else {
        // TODO instrumentMemIntrinsic
      }
    }
  }

  OKF("[%s] %u %s - %s", M.getName().str().c_str(),
      NumOfInstrumentedDereferences.getValue(),
      NumOfInstrumentedDereferences.getName(),
      NumOfInstrumentedDereferences.getDesc());

  return true;
}

static RegisterPass<InstrumentDereferences>
    X("fuzzalloc-instrument-derefs",
      "Instrument pointer dereferences to find their allocation site", false,
      false);

static void registerInstrumentDereferencesPass(const PassManagerBuilder &,
                                               legacy::PassManagerBase &PM) {
  PM.add(new InstrumentDereferences());
}

static RegisterStandardPasses
    RegisterInstrumentDereferencesPass(PassManagerBuilder::EP_OptimizerLast,
                                       registerInstrumentDereferencesPass);

static RegisterStandardPasses RegisterInstrumentDereferencesPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0,
    registerInstrumentDereferencesPass);
