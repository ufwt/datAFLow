//===-- InstrumentMemAccesses.cpp - Instrument memory accesses ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass instruments memory accesses (i.e., \p load and store instructions)
/// to discover their def site.
///
//===----------------------------------------------------------------------===//

#include <set>

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

#include "Common.h"
#include "debug.h" // from afl
#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-inst-mem-accesses"

static cl::opt<bool>
    ClInstrumentWrites("fuzzalloc-instrument-writes",
                       cl::desc("Instrument write instructions"));
static cl::opt<bool>
    ClInstrumentReads("fuzzalloc-instrument-reads",
                      cl::desc("Instrument read instructions"));
static cl::opt<bool> ClInstrumentAtomics(
    "fuzzalloc-instrument-atomics",
    cl::desc("Instrument atomic instructions (rmw, cmpxchg)"));

static cl::opt<bool>
    ClDebugInstrument("fuzzalloc-debug-instrument",
                      cl::desc("Instrument with debug function"), cl::Hidden);

STATISTIC(NumOfInstrumentedMemAccesses,
          "Number of memory accesses instrumented.");

static const char *const DbgInstrumentName = "__mem_access";
static const char *const AFLMapName = "__afl_area_ptr";

namespace {

class InstrumentMemAccesses : public ModulePass {
private:
  IntegerType *Int8Ty;
  IntegerType *Int32Ty;
  IntegerType *Int64Ty;
  IntegerType *TagTy;

  ConstantInt *TagShiftSize;
  ConstantInt *TagMask;
  ConstantInt *AFLInc;
  ConstantInt *HashMul;

  Value *ReadPCAsm;
  GlobalVariable *AFLMapPtr;
  Function *DbgInstrumentFn;

  void doInstrument(Instruction *, Value *);

public:
  static char ID;
  InstrumentMemAccesses() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

char InstrumentMemAccesses::ID = 0;

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkInstrumentationFunc(Constant *FuncOrBitcast) {
  if (isa<Function>(FuncOrBitcast)) {
    return cast<Function>(FuncOrBitcast);
  }

  FuncOrBitcast->print(errs());
  errs() << '\n';
  std::string Err;
  raw_string_ostream OS(Err);
  OS << "Instrumentation function redefined: " << *FuncOrBitcast;
  OS.flush();
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

/// Instrument the Instruction `I` that accesses the memory at `Ptr`.
void InstrumentMemAccesses::doInstrument(Instruction *I, Value *Ptr) {
  LLVM_DEBUG(dbgs() << "instrumenting " << *Ptr << " in " << *I << '\n');

  auto *M = I->getModule();
  IRBuilder<> IRB(I);
  LLVMContext &C = IRB.getContext();

  // This metadata can be used by the static pointer analysis
  I->setMetadata(M->getMDKindID("fuzzalloc.instrumented_deref"),
                 MDNode::get(C, None));

  // Cast the memory access pointer to an integer and mask out the mspace tag
  // from the pointer by right-shifting by 32 bits
  auto *PtrAsInt = IRB.CreatePtrToInt(Ptr, this->Int64Ty);
  if (auto PtrAsIntInst = dyn_cast<Instruction>(PtrAsInt)) {
    PtrAsIntInst->setMetadata(M->getMDKindID("nosanitize"),
                              MDNode::get(C, None));
  }
  auto *MSpaceTag = IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, this->TagShiftSize),
                                  this->TagMask);
  auto *DefSite =
      IRB.CreateIntCast(MSpaceTag, this->TagTy, /* isSigned */ false);

  if (ClDebugInstrument) {
    // For debugging
    IRB.CreateCall(this->DbgInstrumentFn, DefSite);
  } else {
    // Use the PC as the use site identifier
    auto *UseSite =
        IRB.CreateIntCast(IRB.CreateCall(this->ReadPCAsm), this->TagTy,
                          /* isSigned */ false);

    // Load the AFL bitmap
    auto *AFLMap = IRB.CreateLoad(this->AFLMapPtr);
    AFLMap->setMetadata(M->getMDKindID("nosanitize"), MDNode::get(C, None));

    // Hash the allocation site and use site to index into the bitmap
    //
    // zext is necessary otherwise we end up using signed indices
    //
    // Hash algorithm: ((3 * (def_site - DEFAULT_TAG)) ^ use_site) - use_site
    auto *Hash = IRB.CreateSub(
        IRB.CreateXor(
            IRB.CreateMul(this->HashMul,
                          IRB.CreateSub(DefSite, ConstantInt::get(
                                                     this->TagTy,
                                                     FUZZALLOC_DEFAULT_TAG))),
            UseSite),
        UseSite);
    auto *AFLMapIdx =
        IRB.CreateGEP(AFLMap, IRB.CreateZExt(Hash, this->Int32Ty));

    // Update the bitmap only if the def site is not the default tag
    auto *CounterLoad = IRB.CreateLoad(AFLMapIdx);
    CounterLoad->setMetadata(M->getMDKindID("nosanitize"),
                             MDNode::get(C, None));
    auto *Incr = IRB.CreateAdd(CounterLoad, this->AFLInc);
    auto *CounterStore = IRB.CreateStore(Incr, AFLMapIdx);
    CounterStore->setMetadata(M->getMDKindID("nosanitize"),
                              MDNode::get(C, None));
  }

  NumOfInstrumentedMemAccesses++;
}

void InstrumentMemAccesses::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool InstrumentMemAccesses::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  IntegerType *SizeTTy = DL.getIntPtrType(C);

  this->Int8Ty = Type::getInt8Ty(C);
  this->Int32Ty = Type::getInt32Ty(C);
  this->Int64Ty = Type::getInt64Ty(C);
  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);

  this->TagShiftSize =
      ConstantInt::get(SizeTTy, NUM_USABLE_BITS - NUM_TAG_BITS);
  this->TagMask = ConstantInt::get(this->TagTy, (1 << NUM_TAG_BITS) - 1);
  this->AFLInc = ConstantInt::get(this->Int8Ty, 1);
  this->HashMul = ConstantInt::get(this->TagTy, 3);

  return false;
}

bool InstrumentMemAccesses::runOnModule(Module &M) {
  assert((ClInstrumentReads || ClInstrumentWrites) &&
         "Must instrument either loads or stores");

  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  this->ReadPCAsm = InlineAsm::get(
      FunctionType::get(this->Int64Ty, /* isVarArg */ false), "leaq (%rip), $0",
      /* Constraints */ "=r", /* hasSideEffects */ false);
  this->AFLMapPtr = new GlobalVariable(
      M, PointerType::getUnqual(this->Int8Ty), /* isConstant */ false,
      GlobalValue::ExternalLinkage, /* Initializer */ nullptr, AFLMapName);

  this->DbgInstrumentFn = checkInstrumentationFunc(M.getOrInsertFunction(
      DbgInstrumentName, Type::getVoidTy(C), this->TagTy));

  // For determining whether to instrument a memory dereference
  ObjectSizeOpts ObjSizeOpts;
  ObjSizeOpts.RoundToAlign = true;
  ObjectSizeOffsetVisitor ObjSizeVis(DL, TLI, C, ObjSizeOpts);

  for (auto &F : M.functions()) {
    // Don't instrument our own constructors/destructors
    if (F.getName().startswith("fuzzalloc.init_") ||
        F.getName().startswith("fuzzalloc.alloc_") ||
        F.getName().startswith("fuzzalloc.free_")) {
      continue;
    }

    // We want to instrument every address only once per basic block (unless
    // there are calls between uses that access memory)
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
          Value *Obj = GetUnderlyingObject(Addr, DL);

          // If we have a mask, skip instrumentation if we've already
          // instrumented the full object. But don't add to TempsToInstrument
          // because we might get another load/store with a different mask
          if (MaybeMask) {
            if (TempsToInstrument.count(Obj)) {
              // We've seen this (whole) temp in the current BB
              continue;
            }
          } else {
            if (!TempsToInstrument.insert(Obj).second) {
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
            // A call that accesses memory inside the basic block. If the call
            // is indirect (getCalledFunction returns null) then we don't know
            // so we just have to assume that it accesses memory
            auto *CalledF = CS.getCalledFunction();
            bool MaybeAccessMemory =
                CalledF ? !CalledF->doesNotAccessMemory() : true;
            if (MaybeAccessMemory) {
              TempsToInstrument.clear();
            }
          }

          continue;
        }

        // Finally, check if the instruction has the "noinstrument" metadata
        // attached to it (from the array heapify pass)
        if (!Inst.getMetadata(M.getMDKindID("fuzzalloc.noinstrument"))) {
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

        doInstrument(I, Addr);
      } else {
        // TODO instrumentMemIntrinsic
      }
    }
  }

  printStatistic(M, NumOfInstrumentedMemAccesses);

  return NumOfInstrumentedMemAccesses > 0;
}

static RegisterPass<InstrumentMemAccesses>
    X("fuzzalloc-inst-mem-accesses",
      "Instrument memory accesses to find their def site", false, false);

static void registerInstrumentMemAccessesPass(const PassManagerBuilder &,
                                              legacy::PassManagerBase &PM) {
  PM.add(new InstrumentMemAccesses());
}

static RegisterStandardPasses
    RegisterInstrumentMemAccessesPass(PassManagerBuilder::EP_OptimizerLast,
                                      registerInstrumentMemAccessesPass);

static RegisterStandardPasses RegisterInstrumentMemAccessesPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0,
    registerInstrumentMemAccessesPass);
