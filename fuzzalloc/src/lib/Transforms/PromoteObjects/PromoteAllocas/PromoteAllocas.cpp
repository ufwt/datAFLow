//===-- PromoteAllocas.cpp - Promote alloca arrays to mallocs -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass promotes stack-based (i.e., allocas) static arrays to
/// dynamically-allocated arrays via \p malloc.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "Common.h"
#include "PromoteCommon.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-prom-allocas"

static cl::opt<int> ClMinArraySize(
    "fuzzalloc-min-alloca array-size",
    cl::desc("The minimum size of a static alloca array to promote to malloc"),
    cl::init(1));

STATISTIC(NumOfAllocaArrayPromotion, "Number of alloca array promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteAllocas: instrument the code in a module to promote static,
/// fixed-size arrays on the stack (i.e., allocas) to dynamically allocated
/// arrays via \p malloc.
class PromoteAllocas : public ModulePass {
private:
  DataLayout *DL;
  DIBuilder *DBuilder;

  Instruction *insertMalloc(const AllocaInst *, AllocaInst *,
                            Instruction *) const;

  void copyDebugInfo(const AllocaInst *, AllocaInst *) const;

  AllocaInst *promoteAlloca(AllocaInst *,
                            const ArrayRef<IntrinsicInst *> &) const;

public:
  static char ID;
  PromoteAllocas() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteAllocas::ID = 0;

/// Insert a call to `malloc` before the `InsertPt` instruction. The result of
/// the `malloc` call is stored in `NewAlloca`.
Instruction *PromoteAllocas::insertMalloc(const AllocaInst *OrigAlloca,
                                          AllocaInst *NewAlloca,
                                          Instruction *InsertPt) const {
  const Module *M = OrigAlloca->getModule();
  LLVMContext &C = M->getContext();

  ArrayType *ArrayTy = cast<ArrayType>(OrigAlloca->getAllocatedType());
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayNumElems = ArrayTy->getNumElements();

  IRBuilder<> IRB(InsertPt);

  auto *MallocCall =
      createArrayMalloc(C, *this->DL, IRB, ElemTy, ArrayNumElems);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                           MDNode::get(C, None));

  return MallocCall;
}

void PromoteAllocas::copyDebugInfo(const AllocaInst *OrigAlloca,
                                   AllocaInst *NewAlloca) const {
  auto *F = OrigAlloca->getFunction();

  for (auto I = inst_begin(F); I != inst_end(F); ++I) {
    if (auto *DbgDeclare = dyn_cast<DbgDeclareInst>(&*I)) {
      if (DbgDeclare->getAddress() == OrigAlloca) {
        this->DBuilder->insertDeclare(NewAlloca, DbgDeclare->getVariable(),
                                      DbgDeclare->getExpression(),
                                      DbgDeclare->getDebugLoc(),
                                      const_cast<DbgDeclareInst *>(DbgDeclare));
      }
    }
  }
}

AllocaInst *PromoteAllocas::promoteAlloca(
    AllocaInst *Alloca, const ArrayRef<IntrinsicInst *> &LifetimeStarts) const {
  LLVM_DEBUG(dbgs() << "promoting" << *Alloca << " in function "
                    << Alloca->getFunction()->getName() << ")\n");

  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  ArrayType *ArrayTy = cast<ArrayType>(Alloca->getAllocatedType());
  Type *ElemTy = ArrayTy->getArrayElementType();

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
  auto *NewAlloca = new AllocaInst(NewAllocaTy, this->DL->getAllocaAddrSpace(),
                                   Alloca->getName() + "_prom", Alloca);
  copyDebugInfo(Alloca, NewAlloca);

  // Decide where to insert the call to malloc.
  //
  // If there are lifetime.start intrinsics, then we must allocate memory at
  // these intrinsics. Otherwise, we can just perform the allocation after the
  // alloca instruction.
  if (LifetimeStarts.empty()) {
    insertMalloc(Alloca, NewAlloca, NewAlloca->getNextNode());
  } else {
    for (auto *LifetimeStart : LifetimeStarts) {
      if (GetUnderlyingObjectThroughLoads(LifetimeStart->getOperand(1),
                                          *this->DL) == Alloca) {
        auto *Ptr = LifetimeStart->getOperand(1);
        assert(isa<Instruction>(Ptr));

        insertMalloc(Alloca, NewAlloca, cast<Instruction>(Ptr));
      }
    }
  }

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewAlloca);
    } else if (auto *Store = dyn_cast<StoreInst>(U)) {
      // Sometimes the original array may be stored to some temporary variable
      // generated by LLVM (e.g., from a GEP instruction).
      //
      // In this case, we can just cast the new dynamically allocated alloca
      // (which is a pointer) to the original static array's type

      // The original array must be the store's value operand (I think...)
      assert(Store->getValueOperand() == Alloca);

      auto *StorePtrElemTy =
          Store->getPointerOperandType()->getPointerElementType();

      // Only cast the new alloca if the types don't match
      auto *ReplacementAlloca = (StorePtrElemTy == NewAllocaTy)
                                    ? static_cast<Instruction *>(NewAlloca)
                                    : CastInst::CreatePointerCast(
                                          NewAlloca, StorePtrElemTy, "", Store);

      Store->replaceUsesOfWith(Alloca, ReplacementAlloca);
    } else if (auto *Select = dyn_cast<SelectInst>(U)) {
      // Ensure selects are correcty typed
      updateSelect(Select, Alloca, NewAlloca);
    } else if (auto *Inst = dyn_cast<Instruction>(U)) {
      // We must load the array from the heap before we do anything with it
      auto *LoadNewAlloca = new LoadInst(NewAlloca, "", Inst);
      Inst->replaceUsesOfWith(Alloca, LoadNewAlloca);
    } else {
      assert(false && "Unsupported alloca user");
    }
  }

  return NewAlloca;
}

bool PromoteAllocas::doInitialization(Module &M) {
  this->DL = new DataLayout(M.getDataLayout());
  this->DBuilder = new DIBuilder(M, false);

  return false;
}

bool PromoteAllocas::doFinalization(Module &) {
  delete this->DL;

  this->DBuilder->finalize();
  delete this->DBuilder;

  return false;
}

bool PromoteAllocas::runOnModule(Module &M) {
  // Static array allocations to promote
  SmallVector<AllocaInst *, 8> AllocasToPromote;

  // lifetime.start intrinsics that will require calls to mallic to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeStarts;

  // lifetime.end intrinsics that will require calls to free to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeEnds;

  // llvm.mem* intrinsics that may require realignment
  SmallVector<MemIntrinsic *, 4> MemIntrinsics;

  // Return instructions that may require calls to free to be inserted before
  // them
  SmallVector<ReturnInst *, 4> Returns;

  for (auto &F : M.functions()) {
    AllocasToPromote.clear();
    LifetimeStarts.clear();
    LifetimeEnds.clear();
    MemIntrinsics.clear();
    Returns.clear();

    // Collect all the things!
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (isPromotableType(Alloca->getAllocatedType())) {
          AllocasToPromote.push_back(Alloca);
        }
      } else if (auto *MemI = dyn_cast<MemIntrinsic>(&*I)) {
        MemIntrinsics.push_back(MemI);
      } else if (auto *Intrinsic = dyn_cast<IntrinsicInst>(&*I)) {
        if (Intrinsic->getIntrinsicID() == Intrinsic::lifetime_start) {
          LifetimeStarts.push_back(Intrinsic);
        } else if (Intrinsic->getIntrinsicID() == Intrinsic::lifetime_end) {
          LifetimeEnds.push_back(Intrinsic);
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        Returns.push_back(Return);
      }
    }

    // Promote static arrays to dynamically allocated arrays and insert calls
    // to free at the appropriate locations (either at lifetime.end intrinsics
    // or at return instructions)
    for (auto *Alloca : AllocasToPromote) {
      // Promote the alloca. After this function call all users of the original
      // alloca are invalid
      auto *NewAlloca = promoteAlloca(Alloca, LifetimeStarts);

      // Check if any of the original allocas (which have now been replaced by
      // the new alloca) are used in any lifetime.end intrinsics. If they are,
      // insert the free before the lifetime.end intrinsic and NOT at function
      // return, otherwise we may end up with a double free :(
      if (LifetimeEnds.empty()) {
        // If no lifetime.end intrinsics were found, just free the allocation
        // when the function returns
        for (auto *Return : Returns) {
          insertFree(NewAlloca, Return);
          NumOfFreeInsert++;
        }
      } else {
        // Otherwise insert the free before each lifetime.end
        for (auto *LifetimeEnd : LifetimeEnds) {
          if (GetUnderlyingObjectThroughLoads(LifetimeEnd->getOperand(1),
                                              *this->DL) == NewAlloca) {
            insertFree(NewAlloca, LifetimeEnd);
            NumOfFreeInsert++;
          }
        }
      }

      // Array allocas may be memset/memcpy'd to (e.g., when assigned the empty
      // string, etc.). The alignment may be suitable for the old static array,
      // but may break the new dynamically allocated pointer. To be safe we
      // remove any alignment and let LLVM decide what is appropriate
      for (auto *MemI : MemIntrinsics) {
        if (GetUnderlyingObjectThroughLoads(MemI->getDest(), *this->DL) ==
            NewAlloca) {
          MemI->setDestAlignment(0);
        }
      }

      Alloca->eraseFromParent();
      NumOfAllocaArrayPromotion++;
    }
  }

  printStatistic(M, NumOfAllocaArrayPromotion);

  return NumOfAllocaArrayPromotion > 0;
}

static RegisterPass<PromoteAllocas>
    X("fuzzalloc-prom-allocas", "Promote static array allocas to malloc calls",
      false, false);

static void registerPromoteAllocasPass(const PassManagerBuilder &,
                                       legacy::PassManagerBase &PM) {
  PM.add(new PromoteAllocas());
}

static RegisterStandardPasses
    RegisterPromoteAllocasPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
                               registerPromoteAllocasPass);

static RegisterStandardPasses
    RegisterPromoteAllocasPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                registerPromoteAllocasPass);
