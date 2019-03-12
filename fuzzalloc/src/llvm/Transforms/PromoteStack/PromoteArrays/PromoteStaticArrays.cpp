//===-- PromoteStaticArrays.cpp - Promote static arrays to mallocs --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass promotes static arrays (both global and stack-based) to
/// dynamically allocated arrays via \p malloc.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "PromoteCommon.h"

using namespace llvm;

#define DEBUG_TYPE "prom-static-arrays"

static cl::opt<int> ClMinArraySize(
    "fuzzalloc-min-array-size",
    cl::desc("The minimum size of a static array to promote to malloc"),
    cl::init(1));

STATISTIC(NumOfArrayPromotion, "Number of array promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteStaticArrays: instrument the code in a module to promote static,
/// fixed-size arrays (both global and stack-based) to dynamically allocated
/// arrays via \p malloc.
class PromoteStaticArrays : public ModulePass {
private:
  DataLayout *DL;
  IntegerType *IntPtrTy;

  Instruction *createArrayMalloc(IRBuilder<> &, Type *, uint64_t);
  Value *updateGEP(GetElementPtrInst *, Instruction *);
  AllocaInst *promoteArrayAlloca(AllocaInst *);

public:
  static char ID;
  PromoteStaticArrays() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteStaticArrays::ID = 0;

Instruction *PromoteStaticArrays::createArrayMalloc(IRBuilder<> &IRB,
                                                    Type *AllocTy,
                                                    uint64_t ArrayNumElems) {
  uint64_t TypeSize = this->DL->getTypeAllocSize(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), this->IntPtrTy, AllocTy,
                                ConstantInt::get(this->IntPtrTy, TypeSize),
                                ConstantInt::get(this->IntPtrTy, ArrayNumElems),
                                nullptr);
}

Value *PromoteStaticArrays::updateGEP(GetElementPtrInst *GEP,
                                      Instruction *MallocPtr) {
  // Cache uses
  SmallVector<User *, 8> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction. Static arrays use an initial "offset 0" that must be ignored
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *NewGEP = cast<GetElementPtrInst>(IRB.CreateInBoundsGEP(
      Load, SmallVector<Value *, 4>(GEP->idx_begin() + 1, GEP->idx_end()),
      GEP->getName() + "_prom"));

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  GEP->replaceAllUsesWith(NewGEP);

  return NewGEP;
}

AllocaInst *PromoteStaticArrays::promoteArrayAlloca(AllocaInst *Alloca) {
  LLVM_DEBUG(dbgs() << "promoting" << *Alloca << " in function "
                    << Alloca->getFunction()->getName() << ")\n");

  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  ArrayType *ArrayTy = cast<ArrayType>(Alloca->getAllocatedType());
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayNumElems = ArrayTy->getNumElements();

  IRBuilder<> IRB(Alloca);

  // This will transform something like this:
  //
  // %1 = alloca [NumElements x Ty]
  //
  // into something like this:
  //
  // %1 = alloca Ty*
  // %2 = call i8* @malloc(PtrTy Size)
  // %3 = bitcast i8* %2 to Ty*
  // store Ty* %3, Ty** %1
  //
  // Where:
  //
  //  - `Ty` is the array element type
  //  - `NumElements` is the array number of elements
  //  - `PtrTy` is the target's pointer type
  //  - `Size` is the size of the allocated buffer (equivalent to
  //    `NumElements * sizeof(Ty)`)
  PointerType *NewAllocaTy = ElemTy->getPointerTo();
  auto *NewAlloca =
      IRB.CreateAlloca(NewAllocaTy, nullptr, Alloca->getName() + "_prom");
  auto *MallocCall = createArrayMalloc(IRB, ElemTy, ArrayNumElems);
  IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewAlloca);
      GEP->eraseFromParent();
    } else if (auto *Store = dyn_cast<StoreInst>(U)) {
      // Sometimes the original array may be stored to some temporary variable
      // generated by LLVM (e.g., from a GEP instruction).
      //
      // In this case, we can just cast the new dynamically allocated alloca
      // (which is a pointer) to a the original static array type

      // The original array must be the store's value operand (I think...)
      assert(Store->getValueOperand() == Alloca);

      auto *StorePtrElemTy =
          Store->getPointerOperandType()->getPointerElementType();

      // Only cast the new alloca if the types don't match
      auto *AllocaReplace =
          (StorePtrElemTy == NewAllocaTy)
              ? static_cast<Instruction *>(NewAlloca)
              : new BitCastInst(NewAlloca, StorePtrElemTy, "", Store);

      U->replaceUsesOfWith(Alloca, AllocaReplace);
    } else if (auto *Select = dyn_cast<SelectInst>(U)) {
      // Similarly, a temporary variable may be used in a select instruction,
      // which also requires casting.

      // The original array must be one of the select values
      assert(Select->getTrueValue() == Alloca ||
             Select->getFalseValue() == Alloca);

      auto *SelectTy = Select->getType();

      // Only cast the new alloca if the types don't match
      auto *AllocaReplace =
          (SelectTy == NewAllocaTy)
              ? static_cast<Instruction *>(NewAlloca)
              : new BitCastInst(NewAlloca, SelectTy, "", Select);

      U->replaceUsesOfWith(Alloca, AllocaReplace);
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  return NewAlloca;
}

bool PromoteStaticArrays::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();

  this->DL = new DataLayout(M.getDataLayout());
  this->IntPtrTy = this->DL->getIntPtrType(C);

  return false;
}

bool PromoteStaticArrays::doFinalization(Module &) {
  delete this->DL;

  return false;
}

bool PromoteStaticArrays::runOnModule(Module &M) {
  for (auto &F : M.functions()) {
    // Static array allocations to promote
    SmallVector<AllocaInst *, 8> ArrayAllocasToPromote;

    // lifetime.end intrinsics that may require calls to free to be inserted
    // before them
    SmallVector<IntrinsicInst *, 4> LifetimeEnds;

    // Return instructions that may require calls to free to be inserted before
    // them
    SmallVector<ReturnInst *, 4> Returns;

    // Collect allocas to promote + lifetime.end intrinsics + returns to
    // (possibly) insert frees before
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (isa<ArrayType>(Alloca->getAllocatedType())) {
          // TODO do something with escape analysis result
          bool AllocaEscapes = PointerMayBeCaptured(
              Alloca, /* ReturnCaptures */ false, /* StoreCaptures */ true);
          (void)AllocaEscapes;

          ArrayAllocasToPromote.push_back(Alloca);
        }
      } else if (auto *Intrinsic = dyn_cast<IntrinsicInst>(&*I)) {
        if (Intrinsic->getIntrinsicID() == Intrinsic::lifetime_end) {
          LifetimeEnds.push_back(Intrinsic);
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        Returns.push_back(Return);
      }
    }

    // Promote static arrays to dynamically allocated arrays and insert calls
    // to free at the appropriate locations (either at lifetime.end intrinsics
    // or at return instructions)
    SmallVector<IntrinsicInst *, 8> LifetimeEndsToInsertFree;

    for (auto *Alloca : ArrayAllocasToPromote) {
      LifetimeEndsToInsertFree.clear();

      // Check if any of the original allocas are used in any of the
      // lifetime.end intrinsics. If they are, we should insert the free before
      // the lifetime.end intrinsic and NOT at every return, otherwise we may
      // end up with a double free :(
      for (auto *LifetimeEnd : LifetimeEnds) {
        if (GetUnderlyingObject(LifetimeEnd->getOperand(1), *this->DL) ==
            Alloca) {
          LifetimeEndsToInsertFree.push_back(LifetimeEnd);
        }
      }

      // Promote the alloca
      auto *NewAlloca = promoteArrayAlloca(Alloca);

      // If no lifetime.end intrinsics were found, just free the allocation
      // when the function returns. Otherwise, free when the lifetime ends
      if (LifetimeEndsToInsertFree.empty()) {
        for (auto *Return : Returns) {
          insertFree(NewAlloca, Return);
          NumOfFreeInsert++;
        }
      } else {
        for (auto *Inst : LifetimeEndsToInsertFree) {
          insertFree(NewAlloca, Inst);
          NumOfFreeInsert++;
        }
      }

      Alloca->eraseFromParent();
      NumOfArrayPromotion++;
    }
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<PromoteStaticArrays>
    X("prom-static-arrays", "Promote static arrays to malloc calls", false,
      false);

static void registerPromoteStaticArraysPass(const PassManagerBuilder &,
                                            legacy::PassManagerBase &PM) {
  PM.add(new PromoteStaticArrays());
}

static RegisterStandardPasses
    RegisterPromoteStaticArraysPass(PassManagerBuilder::EP_OptimizerLast,
                                    registerPromoteStaticArraysPass);

static RegisterStandardPasses
    RegisterPromoteStaticArraysPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                     registerPromoteStaticArraysPass);
