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

#include <map>
#include <vector>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "Utils.h"

using namespace llvm;

#define DEBUG_TYPE "static-array-prom"

static cl::opt<int> ClMinArraySize(
    "static-array-prom-min-size",
    cl::desc("The minimum size of a static array to promote to malloc"),
    cl::init(-1));

STATISTIC(NumOfAllocaPromotion, "Number of array promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteStaticArrays: instrument the code in a module to promote static,
/// fixed-size arrays (both global and stack-based) to dynamically allocated
/// arrays via \p malloc.
class PromoteStaticArrays : public ModulePass {
private:
  DataLayout *DL;
  Type *IntPtrTy;

  Value *updateGEP(AllocaInst *Alloca, GetElementPtrInst *GEP);
  AllocaInst *promoteArrayAlloca(AllocaInst *Alloca);
  AllocaInst *promoteStructAlloca(AllocaInst *Alloca);

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
static bool structContainsArray(StructType *StructTy) {
  for (auto *Elem : StructTy->elements()) {
    if (auto *StructElem = dyn_cast<StructType>(Elem)) {
      return structContainsArray(StructElem);
    } else if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
      return true;
    }
  }

  return false;
}

char PromoteStaticArrays::ID = 0;

Value *PromoteStaticArrays::updateGEP(AllocaInst *Alloca,
                                      GetElementPtrInst *GEP) {
  // Cache uses before creating more
  std::vector<User *> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and greate a new GEP
  // instruction, ignoring the initial "offset 0" that is used when accessing
  // static arrays
  auto *LoadMalloc = IRB.CreateLoad(Alloca);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      LoadMalloc, std::vector<Value *>(GEP->idx_begin() + 1, GEP->idx_end()));

  // Update all the users of the original GEP instruction to use the updated
  // GEP that is correctly typed for the given alloca instruction
  for (auto *U : Users) {
    U->replaceUsesOfWith(GEP, NewGEP);
  }

  return NewGEP;
}

AllocaInst *PromoteStaticArrays::promoteArrayAlloca(AllocaInst *Alloca) {
  // Cache uses before creating more
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  Type *ArrayTy = Alloca->getAllocatedType();
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayAllocSize = this->DL->getTypeAllocSize(ElemTy);
  uint64_t ArrayNumElems = ArrayTy->getArrayNumElements();

  IRBuilder<> IRB(Alloca);
  LLVMContext &C = IRB.getContext();

  // This will transform something like this:
  //
  // %1 = alloca [NumElements x Ty]
  //
  // into:
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
  auto *NewAlloca = IRB.CreateAlloca(ElemTy->getPointerTo());
  auto *MallocCall = CallInst::CreateMalloc(
      Alloca, this->IntPtrTy, ElemTy,
      ConstantInt::get(this->IntPtrTy, ArrayAllocSize),
      ConstantInt::get(this->IntPtrTy, ArrayNumElems), nullptr);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      updateGEP(NewAlloca, GEP);
      GEP->eraseFromParent();
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  // Save the total size (product of type size and number of array elements) of
  // the original array as metadata
  NewAlloca->setMetadata(
      ARRAY_PROM_NUM_ELEMS_MD,
      MDNode::get(C, ConstantAsMetadata::get(
                         ConstantInt::get(this->IntPtrTy, ArrayNumElems))));

  return NewAlloca;
}

// TODO handle nested structs
AllocaInst *PromoteStaticArrays::promoteStructAlloca(AllocaInst *Alloca) {
  // Cache uses before creating more
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  // This is safe because we already know that the alloca has a struct type
  StructType *StructTy = cast<StructType>(Alloca->getAllocatedType());

  // Maps a static array type (that will be promoted to a dynamically allocated
  // array) to its position/index in the struct type
  std::map<ArrayType *, unsigned> StructArrayElements;

  // The elements of the new struct (i.e., with arrays replaced by pointers to
  // dynamically allocated memory)
  std::vector<Type *> NewStructElements;

  // Save static arrays (and their index/position in the struct) that need to
  // be promoted to dynamically allocated arrays
  {
    unsigned Index = 0;
    for (auto *Elem : StructTy->elements()) {
      if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
        StructArrayElements.emplace(ArrayElem, Index);
        NewStructElements.push_back(
            ArrayElem->getArrayElementType()->getPointerTo());
      } else {
        NewStructElements.push_back(Elem);
      }

      Index++;
    }
  }

  IRBuilder<> IRB(Alloca);
  LLVMContext &C = IRB.getContext();

  // The new struct type (without any static arrays)
  StructType *NewStructTy = StructType::create(
      C, NewStructElements, StructTy->getName(), StructTy->isPacked());

  IntegerType *Int32Ty = Type::getInt32Ty(C);

  auto *NewAlloca = IRB.CreateAlloca(NewStructTy);

  for (auto &ArrayTysWithIndex : StructArrayElements) {
    ArrayType *ArrayTy = ArrayTysWithIndex.first;
    unsigned StructIndex = ArrayTysWithIndex.second;

    Type *ElemTy = ArrayTy->getArrayElementType();
    Value *GEPIndices[] = {ConstantInt::get(Int32Ty, 0),
                           ConstantInt::get(Int32Ty, StructIndex)};

    auto *MallocCall = CallInst::CreateMalloc(
        Alloca, this->IntPtrTy, ElemTy,
        ConstantInt::get(this->IntPtrTy, this->DL->getTypeAllocSize(ElemTy)),
        ConstantInt::get(this->IntPtrTy, ArrayTy->getArrayNumElements()),
        nullptr);
    auto *NewStructGEP = IRB.CreateGEP(NewAlloca, GEPIndices);
    auto *MallocStore = IRB.CreateStore(MallocCall, NewStructGEP);

    // Update all the users of the original struct to use the new struct
    for (auto *U : Users) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        std::vector<User *> GEPUsers(GEP->user_begin(), GEP->user_end());

        IRBuilder<> GEPIRB(GEP);

        auto *NewGEP = IRB.CreateGEP(
            U, std::vector<Value *>(GEP->idx_begin() + 1, GEP->idx_end()));

        for (auto *UU : GEPUsers) {
          UU->replaceUsesOfWith(GEP, NewGEP);
        }

        // TODO erase GEP
      } else {
        U->replaceUsesOfWith(Alloca, NewAlloca);
      }
    }
  }

  // TODO set NewAlloca metadata

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
  // List of static array allocations to promote
  std::vector<AllocaInst *> ArrayAllocasToPromote;

  // List of struct allocations that contain a static array to promote
  std::vector<AllocaInst *> StructAllocasToPromote;

  // List of return instructions that require calls to free to be inserted
  // before them
  std::vector<ReturnInst *> ReturnsToInsertFree;

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        Type *AllocaTy = Alloca->getAllocatedType();

        if (isa<ArrayType>(AllocaTy)) {
          // TODO perform an escape analysis to determine which array allocas
          // to promote
          ArrayAllocasToPromote.push_back(Alloca);
          NumOfAllocaPromotion++;
        } else if (auto *StructTy = dyn_cast<StructType>(AllocaTy)) {
          if (structContainsArray(StructTy)) {
            // TODO perform an escape analysis to determine which struct
            // allocas to promote
            StructAllocasToPromote.push_back(Alloca);
            NumOfAllocaPromotion++;
          }
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        ReturnsToInsertFree.push_back(Return);
        NumOfFreeInsert++;
      }
    }
  }

  LLVMContext &C = M.getContext();

  for (auto *Alloca : ArrayAllocasToPromote) {
    auto *NewAlloca = promoteArrayAlloca(Alloca);
    Alloca->eraseFromParent();

    // Ensure that the promoted alloca (now dynamically allocated) is freed
    // when the function returns
    for (auto *Return : ReturnsToInsertFree) {
      insertFree(NewAlloca, Return);
    }
  }

  for (auto *Alloca : StructAllocasToPromote) {
    auto *NewAlloca = promoteStructAlloca(Alloca);
    // TODO erase Alloca

    // TODO insert frees
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<PromoteStaticArrays>
    X("static-array-prom", "Promote static array to malloc calls", false,
      false);