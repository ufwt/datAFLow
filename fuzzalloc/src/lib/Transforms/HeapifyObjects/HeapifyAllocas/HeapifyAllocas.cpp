//===-- HeapifyAllocas.cpp - Heapify alloca arrays ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass heapifies stack-based (i.e., allocas) static arrays to
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
#include "HeapifyCommon.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-heapify-allocas"

static cl::opt<int> ClMinArraySize(
    "fuzzalloc-min-alloca array-size",
    cl::desc("The minimum size of a static alloca array to heapify to malloc"),
    cl::init(1));

STATISTIC(NumOfAllocaArrayHeapification,
          "Number of alloca array heapifications.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// HeapifyAllocas: instrument the code in a module to heapify static,
/// fixed-size arrays on the stack (i.e., allocas) to dynamically allocated
/// arrays via \p malloc.
class HeapifyAllocas : public ModulePass {
private:
  DataLayout *DL;
  DIBuilder *DBuilder;

  Instruction *insertMalloc(const AllocaInst *, AllocaInst *,
                            Instruction *) const;

  void copyDebugInfo(const AllocaInst *, AllocaInst *) const;

  AllocaInst *heapifyAlloca(AllocaInst *,
                            const ArrayRef<IntrinsicInst *> &) const;

public:
  static char ID;
  HeapifyAllocas() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char HeapifyAllocas::ID = 0;

/// Insert a call to `malloc` before the `InsertPt` instruction. The result of
/// the `malloc` call is stored in `NewAlloca`.
Instruction *HeapifyAllocas::insertMalloc(const AllocaInst *OrigAlloca,
                                          AllocaInst *NewAlloca,
                                          Instruction *InsertPt) const {
  const Module *M = OrigAlloca->getModule();
  LLVMContext &C = M->getContext();

  ArrayType *ArrayTy = cast<ArrayType>(OrigAlloca->getAllocatedType());
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayNumElems = ArrayTy->getNumElements();

  IRBuilder<> IRB(InsertPt);

  auto *MallocCall = createArrayMalloc(C, *this->DL, IRB, ElemTy, ArrayNumElems,
                                       OrigAlloca->getName() + "_malloccall");
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                           MDNode::get(C, None));

  return MallocCall;
}

void HeapifyAllocas::copyDebugInfo(const AllocaInst *OrigAlloca,
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

AllocaInst *HeapifyAllocas::heapifyAlloca(
    AllocaInst *Alloca, const ArrayRef<IntrinsicInst *> &LifetimeStarts) const {
  LLVM_DEBUG(dbgs() << "heapifying " << *Alloca << " in function "
                    << Alloca->getFunction()->getName() << "\n");

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
                                   Alloca->getName() + "_heapify", Alloca);
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
    } else if (auto *Return = dyn_cast<ReturnInst>(U)) {
      // Ensure returns are correctly typed to the funtion type
      updateReturn(Return, Alloca, NewAlloca);
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

bool HeapifyAllocas::doInitialization(Module &M) {
  this->DL = new DataLayout(M.getDataLayout());
  this->DBuilder = new DIBuilder(M, /* AllowUnresolved */ false);

  return false;
}

bool HeapifyAllocas::doFinalization(Module &) {
  delete this->DL;

  this->DBuilder->finalize();
  delete this->DBuilder;

  return false;
}

bool HeapifyAllocas::runOnModule(Module &M) {
  // Static array allocations to heapify
  SmallVector<AllocaInst *, 8> AllocasToHeapify;

  // lifetime.start intrinsics that will require calls to mallic to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeStarts;

  // lifetime.end intrinsics that will require calls to free to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeEnds;

  // Load instructions that may require realignment
  SmallVector<LoadInst *, 4> Loads;

  // Store instructions that may require realignment
  SmallVector<StoreInst *, 4> Stores;

  // llvm.mem* intrinsics that may require realignment
  SmallVector<MemIntrinsic *, 4> MemIntrinsics;

  // Return instructions that may require calls to free to be inserted before
  // them
  SmallVector<ReturnInst *, 4> Returns;

  for (auto &F : M.functions()) {
    AllocasToHeapify.clear();
    LifetimeStarts.clear();
    LifetimeEnds.clear();
    MemIntrinsics.clear();
    Loads.clear();
    Stores.clear();
    Returns.clear();

    // Collect all the things!
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (isHeapifiableType(Alloca->getAllocatedType())) {
          AllocasToHeapify.push_back(Alloca);
        }
      } else if (auto *Load = dyn_cast<LoadInst>(&*I)) {
        Loads.push_back(Load);
      } else if (auto *Store = dyn_cast<StoreInst>(&*I)) {
        Stores.push_back(Store);
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

    // Heapify static arrays to dynamically allocated arrays and insert calls
    // to free at the appropriate locations (either at lifetime.end intrinsics
    // or at return instructions)
    for (auto *Alloca : AllocasToHeapify) {
      // Heapify the alloca. After this function call all users of the original
      // alloca are invalid
      auto *NewAlloca = heapifyAlloca(Alloca, LifetimeStarts);

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

      // Loads and stores to the newly-heapified allocas may not be aligned
      // correctly for memory on the heap. To be safe we set the alignment to 1,
      // which is "always safe" (according to the LLVM docs)

      for (auto *Load : Loads) {
        if (GetUnderlyingObjectThroughLoads(Load->getPointerOperand(),
                                            *this->DL) == NewAlloca) {
          Load->setAlignment(1);
        }
      }

      for (auto *Store : Stores) {
        if (GetUnderlyingObjectThroughLoads(Store->getPointerOperand(),
                                            *this->DL) == NewAlloca) {
          Store->setAlignment(1);
        }
      }

      for (auto *MemI : MemIntrinsics) {
        if (GetUnderlyingObjectThroughLoads(MemI->getDest(), *this->DL) ==
            NewAlloca) {
          MemI->setDestAlignment(1);
        }
      }

      Alloca->eraseFromParent();
      NumOfAllocaArrayHeapification++;
    }
  }

  printStatistic(M, NumOfAllocaArrayHeapification);
  printStatistic(M, NumOfFreeInsert);

  return NumOfAllocaArrayHeapification > 0;
}

static RegisterPass<HeapifyAllocas>
    X("fuzzalloc-heapify-allocas",
      "Heapify static array allocas to malloc calls", false, false);

static void registerHeapifyAllocasPass(const PassManagerBuilder &,
                                       legacy::PassManagerBase &PM) {
  PM.add(new HeapifyAllocas());
}

static RegisterStandardPasses
    RegisterHeapifyAllocasPass(PassManagerBuilder::EP_ModuleOptimizerEarly,
                               registerHeapifyAllocasPass);

static RegisterStandardPasses
    RegisterHeapifyAllocasPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                registerHeapifyAllocasPass);
