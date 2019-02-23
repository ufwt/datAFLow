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

#include <set>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "static-array-prom"

static cl::opt<int> ClMinArraySize(
    "static-array-prom-min-size",
    cl::desc("The minimum size of a static array to promote to malloc"),
    cl::init(1));

STATISTIC(NumOfArrayPromotion, "Number of array promotions.");
STATISTIC(NumOfStructPromotion, "Number of struct promotions.");
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
  Instruction *createStructMalloc(IRBuilder<> &, StructType *);

  Value *updateGEP(GetElementPtrInst *, Instruction *, bool);

  AllocaInst *promoteArrayAlloca(AllocaInst *);
  AllocaInst *promoteStructAlloca(AllocaInst *);

public:
  static char ID;
  PromoteStaticArrays() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

/// Returns \p true if the struct contains a nested array, or \p false
/// othewise.
///
/// Nested structs are also checked.
static bool structContainsArray(const StructType *StructTy) {
  for (auto *Elem : StructTy->elements()) {
    if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
      if (ArrayElem->getNumElements() >= ClMinArraySize) {
        return true;
      }
    } else if (auto *StructElem = dyn_cast<StructType>(Elem)) {
      return structContainsArray(StructElem);
    }
  }

  return false;
}

static void insertFree(Instruction *MallocPtr, ReturnInst *Return) {
  IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(MallocPtr);
  CallInst::CreateFree(LoadMalloc, Return);
}

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

Instruction *PromoteStaticArrays::createStructMalloc(IRBuilder<> &IRB,
                                                     StructType *AllocTy) {
  Constant *SizeOfStruct = ConstantExpr::getSizeOf(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), this->IntPtrTy, AllocTy,
                                SizeOfStruct,
                                ConstantInt::get(this->IntPtrTy, 1), nullptr);
}

Value *PromoteStaticArrays::updateGEP(GetElementPtrInst *GEP,
                                      Instruction *MallocPtr, bool IsArray) {
  // Cache uses
  SmallVector<User *, 8> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction. Static arrays use an initial "offset 0" that must be ignored
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto IdxBegin = GEP->idx_begin() + (IsArray ? 1 : 0);
  auto *NewGEP = cast<GetElementPtrInst>(IRB.CreateInBoundsGEP(
      Load, SmallVector<Value *, 4>(IdxBegin, GEP->idx_end()),
      GEP->getName() + "_prom"));

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  GEP->replaceAllUsesWith(NewGEP);

  return NewGEP;
}

AllocaInst *PromoteStaticArrays::promoteArrayAlloca(AllocaInst *Alloca) {
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
  auto *NewAlloca = IRB.CreateAlloca(ElemTy->getPointerTo(), nullptr,
                                     Alloca->getName() + "_prom");
  auto *MallocCall = createArrayMalloc(IRB, ElemTy, ArrayNumElems);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewAlloca, /* IsArray */ true);
      GEP->eraseFromParent();
    } else if (auto *Store = dyn_cast<StoreInst>(U)) {
      // Sometimes the original array may be stored to some temporary variable
      // generated by LLVM (e.g., from a GEP instruction).
      //
      // In this case, we can just cast the new dynamically allocated alloca
      // (which is a pointer) to a the original static array type

      // The original array can only be the store's value operand (I think...)
      assert(Store->getValueOperand() == Alloca);

      auto *StorePtrTy = Store->getPointerOperandType();
      auto *BitCastNewAlloca = new BitCastInst(
          NewAlloca, StorePtrTy->getPointerElementType(), "", Store);

      U->replaceUsesOfWith(Alloca, BitCastNewAlloca);
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  return NewAlloca;
}

AllocaInst *PromoteStaticArrays::promoteStructAlloca(AllocaInst *Alloca) {
  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  // This is safe because we already know that the alloca has a struct type
  StructType *StructTy = cast<StructType>(Alloca->getAllocatedType());

  IRBuilder<> IRB(Alloca);

  auto *NewAlloca = IRB.CreateAlloca(StructTy->getPointerTo(), nullptr,
                                     Alloca->getName() + "_prom");
  auto *MallocCall = createStructMalloc(IRB, StructTy);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original struct to use the dynamically
  // allocated structs
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewAlloca, /* IsArray */ false);
      GEP->eraseFromParent();
    } else if (auto *Call = dyn_cast<CallInst>(U)) {
      // Dereference escaped structs
      // TODO escape analysis
      auto *MallocLoad =
          new LoadInst(NewAlloca, NewAlloca->getName() + "_deref", Call);
      U->replaceUsesOfWith(Alloca, MallocLoad);
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
  std::set<StructType *> StructsToPromote;

  TypeFinder StructTypes;
  StructTypes.run(M, /* OnlyNamed */ false);

  for (auto *Ty : StructTypes) {
    if (structContainsArray(Ty)) {
      StructsToPromote.insert(Ty);
    }
  }

  for (auto &F : M.functions()) {
    // List of static array allocations to promote
    SmallVector<AllocaInst *, 8> ArrayAllocasToPromote;

    // List of struct allocations that contain a static array to promote
    SmallVector<AllocaInst *, 8> StructAllocasToPromote;

    // List of return instructions that require calls to free to be inserted
    // before them
    SmallVector<ReturnInst *, 4> ReturnsToInsertFree;

    // Collect things to promote
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        Type *AllocaTy = Alloca->getAllocatedType();

        if (isa<ArrayType>(AllocaTy)) {
          bool AllocaEscapes = PointerMayBeCaptured(
              Alloca, /* ReturnCaptures */ false, /* StoreCaptures */ true);
          // TODO do something with escape analysis result

          ArrayAllocasToPromote.push_back(Alloca);
          NumOfArrayPromotion++;
        } else if (auto *StructTy = dyn_cast<StructType>(AllocaTy)) {
          if (StructsToPromote.find(StructTy) != StructsToPromote.end()) {
            bool AllocaEscapes = PointerMayBeCaptured(
                Alloca, /* ReturnCaptures */ false, /* StoreCaptures */ true);
            // TODO do something with escape analysis result

            StructAllocasToPromote.push_back(Alloca);
            NumOfStructPromotion++;
          }
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        ReturnsToInsertFree.push_back(Return);
        NumOfFreeInsert++;
      }
    }

    // Promote static arrays to dynamically allocated arrays
    for (auto *Alloca : ArrayAllocasToPromote) {
      auto *NewAlloca = promoteArrayAlloca(Alloca);

      // Ensure that the promoted alloca (now dynamically allocated) is freed
      // when the function returns
      for (auto *Return : ReturnsToInsertFree) {
        insertFree(NewAlloca, Return);
      }

      Alloca->eraseFromParent();
    }

    // Promote structs containing static arrays to dynamically allocated
    // structs
    for (auto *Alloca : StructAllocasToPromote) {
      auto *NewAlloca = promoteStructAlloca(Alloca);

      // TODO insert frees

      Alloca->eraseFromParent();
    }
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<PromoteStaticArrays>
    X("static-array-prom", "Promote static array to malloc calls", false,
      false);
