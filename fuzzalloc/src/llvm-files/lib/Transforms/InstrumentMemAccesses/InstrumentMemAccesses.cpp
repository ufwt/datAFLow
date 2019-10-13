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
#include "llvm/Transforms/Utils/ModuleUtils.h"
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

static cl::opt<bool> ClLibFuzzerInstrument("fuzzalloc-libfuzzer",
                                           cl::desc("Instrument for libFuzzer"),
                                           cl::Hidden);

STATISTIC(NumOfInstrumentedMemAccesses,
          "Number of memory accesses instrumented.");

// AFL-style fuzzing
static const char *const DbgInstrumentName = "__mem_access";
static const char *const AFLMapName = "__afl_area_ptr";

// libFuzzer-style fuzzing
static const char *const SanCovModuleCtorName = "sancov.module_ctor";
static const char *const SanCov8bitCountersInitName =
    "__sanitizer_cov_8bit_counters_init";
static const char *const SanCovCountersSectionName = "sancov_cntrs";
static const uint64_t SanCtorAndDtorPriority = 2;

namespace {

class InstrumentMemAccesses : public ModulePass {
private:
  DataLayout *DL;
  Triple TargetTriple;

  IntegerType *Int8Ty;
  IntegerType *Int64Ty;
  IntegerType *IntPtrTy;
  IntegerType *TagTy;

  ConstantInt *TagShiftSize;
  ConstantInt *TagMask;
  ConstantInt *AFLInc;
  ConstantInt *HashMul;

  Value *ReadPCAsm;
  GlobalVariable *AFLMapPtr;
  Function *DbgInstrumentFn;

  Value *isInterestingMemoryAccess(Instruction *, bool *, uint64_t *,
                                   unsigned *, Value **);

public:
  static char ID;
  InstrumentMemAccesses() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;

private:
  //
  // AFL-style fuzzing
  //

  void doAFLInstrument(Instruction *, Value *);

  //
  // libFuzzer-style fuzzing
  //

  GlobalVariable *Function8bitCounterArray;
  SmallVector<GlobalValue *, 20> GlobalsToAppendToCompilerUsed;

  std::string getSectionName(const std::string &) const;
  std::string getSectionStart(const std::string &) const;
  std::string getSectionEnd(const std::string &) const;

  Function *createInitCallsForSections(Module &, const char *, Type *,
                                       const char *);
  GlobalVariable *createFunctionLocalArrayInSection(size_t, Function &, Type *,
                                                    const char *);
  std::pair<GlobalVariable *, GlobalVariable *>
  createSecStartEnd(Module &, const char *, Type *);

  void initializeLibFuzzer(Module &);
  void doLibFuzzerInstrument(Instruction *, Value *);
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
Value *InstrumentMemAccesses::isInterestingMemoryAccess(
    Instruction *I, bool *IsWrite, uint64_t *TypeSize, unsigned *Alignment,
    Value **MaybeMask = nullptr) {
  Value *PtrOperand = nullptr;

  if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (!ClInstrumentReads) {
      return nullptr;
    }

    *IsWrite = false;
    *TypeSize = this->DL->getTypeStoreSizeInBits(LI->getType());
    *Alignment = LI->getAlignment();
    PtrOperand = LI->getPointerOperand();
  } else if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (!ClInstrumentWrites) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize =
        this->DL->getTypeStoreSizeInBits(SI->getValueOperand()->getType());
    *Alignment = SI->getAlignment();
    PtrOperand = SI->getPointerOperand();
  } else if (auto *RMW = dyn_cast<AtomicRMWInst>(I)) {
    if (!ClInstrumentAtomics) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize =
        this->DL->getTypeStoreSizeInBits(RMW->getValOperand()->getType());
    *Alignment = 0;
    PtrOperand = RMW->getPointerOperand();
  } else if (auto *XCHG = dyn_cast<AtomicCmpXchgInst>(I)) {
    if (!ClInstrumentAtomics) {
      return nullptr;
    }

    *IsWrite = true;
    *TypeSize =
        this->DL->getTypeStoreSizeInBits(XCHG->getCompareOperand()->getType());
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
      *TypeSize = this->DL->getTypeStoreSizeInBits(Ty);

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
void InstrumentMemAccesses::doAFLInstrument(Instruction *I, Value *Ptr) {
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
    setNoSanitizeMetadata(PtrAsIntInst);
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
        IRB.CreateGEP(AFLMap, IRB.CreateZExt(Hash, IRB.getInt32Ty()));

    // Update the bitmap only if the def site is not the default tag
    auto *CounterLoad = IRB.CreateLoad(AFLMapIdx);
    auto *Incr = IRB.CreateAdd(CounterLoad, this->AFLInc);
    auto *CounterStore = IRB.CreateStore(Incr, AFLMapIdx);

    setNoSanitizeMetadata(AFLMap);
    setNoSanitizeMetadata(CounterLoad);
    setNoSanitizeMetadata(CounterStore);
  }

  NumOfInstrumentedMemAccesses++;
}

void InstrumentMemAccesses::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool InstrumentMemAccesses::doInitialization(Module &M) {
  this->DL = new DataLayout(M.getDataLayout());
  this->TargetTriple = Triple(M.getTargetTriple());

  LLVMContext &C = M.getContext();
  IntegerType *SizeTTy = this->DL->getIntPtrType(C);

  this->Int8Ty = Type::getInt8Ty(C);
  this->Int64Ty = Type::getInt64Ty(C);
  this->IntPtrTy = Type::getIntNTy(C, this->DL->getPointerSizeInBits());
  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);

  this->TagShiftSize = ConstantInt::get(SizeTTy, FUZZALLOC_TAG_SHIFT);
  this->TagMask = ConstantInt::get(this->TagTy, FUZZALLOC_TAG_MASK);
  this->AFLInc = ConstantInt::get(this->Int8Ty, 1);
  this->HashMul = ConstantInt::get(this->TagTy, 3);

  this->Function8bitCounterArray = nullptr;

  return false;
}

bool InstrumentMemAccesses::runOnModule(Module &M) {
  assert((ClInstrumentReads || ClInstrumentWrites) &&
         "Must instrument either loads or stores");

  LLVMContext &C = M.getContext();
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  this->ReadPCAsm = InlineAsm::get(
      FunctionType::get(this->Int64Ty, /* isVarArg */ false), "leaq (%rip), $0",
      /* Constraints */ "=r", /* hasSideEffects */ false);
  this->AFLMapPtr = new GlobalVariable(
      M, PointerType::getUnqual(this->Int8Ty), /* isConstant */ false,
      GlobalValue::ExternalLinkage, /* Initializer */ nullptr, AFLMapName);

  if (ClDebugInstrument) {
    this->DbgInstrumentFn = checkInstrumentationFunc(M.getOrInsertFunction(
        DbgInstrumentName, Type::getVoidTy(C), this->TagTy));
  }

  // For determining whether to instrument a memory dereference
  ObjectSizeOpts ObjSizeOpts;
  ObjSizeOpts.RoundToAlign = true;
  ObjectSizeOffsetVisitor ObjSizeVis(*this->DL, TLI, C, ObjSizeOpts);

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
          Value *Obj = GetUnderlyingObject(Addr, *this->DL);

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

    // Nothing to instrument in this function
    if (ToInstrument.empty()) {
      continue;
    }

    // Adapted from llvm::SanitizerCoverage::CreateFunctionLocalArrays
    if (ClLibFuzzerInstrument) {
      this->Function8bitCounterArray = createFunctionLocalArrayInSection(
          ToInstrument.size(), F, this->Int8Ty, SanCovCountersSectionName);
      GlobalsToAppendToCompilerUsed.push_back(this->Function8bitCounterArray);
      MDNode *MD = MDNode::get(F.getContext(), ValueAsMetadata::get(&F));
      this->Function8bitCounterArray->addMetadata(LLVMContext::MD_associated,
                                                  *MD);
    }

    // Instrument memory operations
    for (auto *I : ToInstrument) {
      if (Value *Addr =
              isInterestingMemoryAccess(I, &IsWrite, &TypeSize, &Alignment)) {
        // A direct inbounds access to a stack variable is always valid
        if (isa<AllocaInst>(GetUnderlyingObject(Addr, *this->DL)) &&
            isSafeAccess(ObjSizeVis, Addr, TypeSize)) {
          continue;
        }

        if (ClLibFuzzerInstrument) {
          doLibFuzzerInstrument(I, Addr);
        } else {
          doAFLInstrument(I, Addr);
        }
      } else {
        // TODO instrumentMemIntrinsic
      }
    }
  }

  if (ClLibFuzzerInstrument) {
    initializeLibFuzzer(M);
  }

  printStatistic(M, NumOfInstrumentedMemAccesses);

  return NumOfInstrumentedMemAccesses > 0;
}

//===----------------------------------------------------------------------===//
//
// libFuzzer-style fuzzing
//
//===----------------------------------------------------------------------===//

// Adapted from llvm::SanitizerCoverageModule::getSectionName
std::string
InstrumentMemAccesses::getSectionName(const std::string &Section) const {
  if (this->TargetTriple.getObjectFormat() == Triple::COFF) {
    return ".SCOV$M";
  }
  if (this->TargetTriple.isOSBinFormatMachO()) {
    return "__DATA,__" + Section;
  }

  return "__" + Section;
}

std::string
InstrumentMemAccesses::getSectionStart(const std::string &Section) const {
  if (this->TargetTriple.isOSBinFormatMachO()) {
    return "\1section$start$__DATA$__" + Section;
  }

  return "__start___" + Section;
}

std::string
InstrumentMemAccesses::getSectionEnd(const std::string &Section) const {
  if (this->TargetTriple.isOSBinFormatMachO()) {
    return "\1section$end$__DATA$__" + Section;
  }

  return "__stop___" + Section;
}

// Adapted from llvm::SanitizerCoverageModule::CreateSecStartEnd
std::pair<GlobalVariable *, GlobalVariable *>
InstrumentMemAccesses::createSecStartEnd(Module &M, const char *Section,
                                         Type *Ty) {
  GlobalVariable *SecStart = new GlobalVariable(
      M, Ty, /* isConstant */ false, GlobalVariable::ExternalLinkage,
      /* Initializer */ nullptr, getSectionStart(Section));
  SecStart->setVisibility(GlobalValue::HiddenVisibility);
  GlobalVariable *SecEnd = new GlobalVariable(
      M, Ty, /* isConstant */ false, GlobalVariable::ExternalLinkage,
      /* Initializer */ nullptr, getSectionEnd(Section));
  SecEnd->setVisibility(GlobalValue::HiddenVisibility);

  return std::make_pair(SecStart, SecEnd);
}

// Adapted from llvm::SanitizerCoverageModule::CreateFunctionLocalArrayInSection
GlobalVariable *InstrumentMemAccesses::createFunctionLocalArrayInSection(
    size_t NumElements, Function &F, Type *Ty, const char *Section) {
  Module *M = F.getParent();
  ArrayType *ArrayTy = ArrayType::get(Ty, NumElements);
  auto Array = new GlobalVariable(
      *M, ArrayTy, /* isConstant */ false, GlobalVariable::PrivateLinkage,
      Constant::getNullValue(ArrayTy), "__sancov_gen");
  if (auto Comdat = F.getComdat()) {
    Array->setComdat(Comdat);
  }
  Array->setSection(getSectionName(Section));
  Array->setAlignment(Ty->isPointerTy() ? this->DL->getPointerSize()
                                        : Ty->getPrimitiveSizeInBits() / 8);

  return Array;
}

// Adapted from llvm::SanitizerCoverageModule::CreateInitCallsForSections
Function *InstrumentMemAccesses::createInitCallsForSections(
    Module &M, const char *InitFunctionName, Type *Ty, const char *Section) {
  IRBuilder<> IRB(M.getContext());
  auto SecStartEnd = createSecStartEnd(M, Section, Ty);
  auto SecStart = SecStartEnd.first;
  auto SecEnd = SecStartEnd.second;
  Function *CtorFunc;
  std::tie(CtorFunc, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, SanCovModuleCtorName, InitFunctionName, {Ty, Ty},
      {IRB.CreatePointerCast(SecStart, Ty), IRB.CreatePointerCast(SecEnd, Ty)});

  if (TargetTriple.supportsCOMDAT()) {
    // Use comdat to dedup CtorFunc
    CtorFunc->setComdat(M.getOrInsertComdat(SanCovModuleCtorName));
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority, CtorFunc);
  } else {
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority);
  }

  return CtorFunc;
}

// Adapted from llvm::SanitizerCoverageModule::runOnModule
void InstrumentMemAccesses::initializeLibFuzzer(Module &M) {
  Function *Ctor = nullptr;

  if (this->Function8bitCounterArray) {
    Ctor = createInitCallsForSections(M, SanCov8bitCountersInitName,
                                      this->Int8Ty->getPointerTo(),
                                      SanCovCountersSectionName);
  }

  // We don't reference these arrays directly in any of our runtime functions,
  // so we need to prevent them from being dead stripped
  appendToCompilerUsed(M, this->GlobalsToAppendToCompilerUsed);
}

// Adapted from llvm::SanitizerCoverageModule::InjectCoverageAtBlock
void InstrumentMemAccesses::doLibFuzzerInstrument(Instruction *I, Value *Ptr) {
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
    setNoSanitizeMetadata(PtrAsIntInst);
  }
  auto *MSpaceTag = IRB.CreateAnd(IRB.CreateLShr(PtrAsInt, this->TagShiftSize),
                                  this->TagMask);
  auto *DefSite =
      IRB.CreateIntCast(MSpaceTag, this->TagTy, /* isSigned */ false);
  (void)DefSite;

  // Adapted from llvm::SanitizerCoverageModule::InjectCoverateAtBlock

  auto *CounterPtr =
      IRB.CreateGEP(this->Function8bitCounterArray,
                    {ConstantInt::get(this->IntPtrTy, 0),
                     ConstantInt::get(this->IntPtrTy, /* FIXME */ 0)});
  auto *Load = IRB.CreateLoad(CounterPtr);
  auto *Inc = IRB.CreateAdd(Load, ConstantInt::get(this->Int8Ty, 1));
  auto Store = IRB.CreateStore(Inc, CounterPtr);

  setNoSanitizeMetadata(Load);
  setNoSanitizeMetadata(Store);

  NumOfInstrumentedMemAccesses++;
}

//===----------------------------------------------------------------------===//

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
