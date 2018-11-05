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
  IntegerType *IntPtrTy;
  IntegerType *Int32Ty;

  Instruction *createMalloc(Instruction *InsertBefore, Type *AllocTy,
                            uint64_t ArrayNumElems);

  Value *updateArrayGEP(Instruction *MallocPtr, GetElementPtrInst *GEP);
  Value *updateStructGEP(Instruction *MallocPtr, GetElementPtrInst *GEP);

  AllocaInst *promoteArrayAlloca(AllocaInst *Alloca);
  std::pair<AllocaInst *, std::vector<unsigned>>
  promoteStructAlloca(AllocaInst *Alloca);

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

void insertFree(Instruction *MallocPtr, ReturnInst *Return) {
  IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(MallocPtr);
  CallInst::CreateFree(LoadMalloc, Return);
}

char PromoteStaticArrays::ID = 0;

Instruction *PromoteStaticArrays::createMalloc(Instruction *InsertBefore,
                                               Type *AllocTy,
                                               uint64_t ArrayNumElems) {
  uint64_t TypeSize = this->DL->getTypeAllocSize(AllocTy);

  return CallInst::CreateMalloc(InsertBefore, this->IntPtrTy, AllocTy,
                                ConstantInt::get(this->IntPtrTy, TypeSize),
                                ConstantInt::get(this->IntPtrTy, ArrayNumElems),
                                nullptr);
}

Value *PromoteStaticArrays::updateArrayGEP(Instruction *MallocPtr,
                                           GetElementPtrInst *GEP) {
  // Cache uses
  std::vector<User *> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction (based on the original static array GEP). The initial "offset
  // 0" that is used when accessing static arrays is ignored
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      Load, std::vector<Value *>(GEP->idx_begin() + 1, GEP->idx_end()));

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  for (auto *U : Users) {
    U->replaceUsesOfWith(GEP, NewGEP);
  }

  return NewGEP;
}

Value *PromoteStaticArrays::updateStructGEP(Instruction *MallocPtr,
                                            GetElementPtrInst *GEP) {
  // Cache uses
  std::vector<User *> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Calculate the address of a dynamically allocated array in a struct. This
  // calculation is based on the original GEP instruction. The original GEP
  // instruction is typed for a static array in the old struct, whilst the new
  // GEP instruction will be correctly typed for the malloc pointer
  auto *NewGEP = cast<GetElementPtrInst>(IRB.CreateInBoundsGEP(
      MallocPtr, std::vector<Value *>(GEP->idx_begin(), GEP->idx_end())));

  // Update all the users of the original GEP instruction to use the updated
  // GEP instruction
  for (auto *U : Users) {
    if (auto *ArrayGEP = dyn_cast<GetElementPtrInst>(U)) {
      // GEPs are handled separately to ensure that they are correctly typed
      updateArrayGEP(cast<Instruction>(NewGEP), ArrayGEP);
      ArrayGEP->eraseFromParent();
    } else {
      U->replaceUsesOfWith(GEP, NewGEP);
    }
  }

  return NewGEP;
}

AllocaInst *PromoteStaticArrays::promoteArrayAlloca(AllocaInst *Alloca) {
  // Cache uses
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  Type *ArrayTy = Alloca->getAllocatedType();
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayNumElems = ArrayTy->getArrayNumElements();

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
  auto *NewAlloca = IRB.CreateAlloca(ElemTy->getPointerTo());
  auto *MallocCall = createMalloc(Alloca, ElemTy, ArrayNumElems);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // GEPs are handled separately to ensure that they are correctly typed
      updateArrayGEP(NewAlloca, GEP);
      GEP->eraseFromParent();
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  return NewAlloca;
}

// TODO generalise for nested structs
std::pair<AllocaInst *, std::vector<unsigned>>
PromoteStaticArrays::promoteStructAlloca(AllocaInst *Alloca) {
  // Cache uses
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  // This is safe because we already know that the alloca has a struct type
  StructType *StructTy = cast<StructType>(Alloca->getAllocatedType());

  // Maps the type of a static array (that will be promoted to a dynamically
  // allocated array) to its position in (i.e., the index of) the struct
  std::map<ArrayType *, unsigned> StructArrayElements;

  // The elements of the new struct (i.e., with static arrays replaced by
  // pointers to dynamically allocated memory)
  std::vector<Type *> NewStructElements;

  // Record all of the static arrays (and their index/position in the struct)
  // that are to be promoted to dynamically allocated arrays
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

  // The new struct type (with static arrays replaced by pointers)
  StructType *NewStructTy = StructType::create(
      C, NewStructElements, StructTy->getName(), StructTy->isPacked());

  auto *NewAlloca = IRB.CreateAlloca(NewStructTy);

  // Keep track of the position (i.e., index) of a promoted array in the struct
  std::vector<unsigned> PromotedElemIndices;

  // Promote each of the static arrays found in the struct to dynamically
  // allocated arrays
  for (auto &ArrayTysWithIndex : StructArrayElements) {
    ArrayType *ArrayTy = ArrayTysWithIndex.first;
    unsigned StructIndex = ArrayTysWithIndex.second;

    Type *ElemTy = ArrayTy->getArrayElementType();

    uint64_t ArrayNumElems = ArrayTy->getArrayNumElements();

    // As per https://llvm.org/docs/GetElementPtr.html, struct member indices
    // always use i32
    Value *GEPIndices[] = {ConstantInt::get(this->Int32Ty, 0),
                           ConstantInt::get(this->Int32Ty, StructIndex)};

    auto *MallocCall = createMalloc(Alloca, ElemTy, ArrayNumElems);
    auto *NewStructGEP = IRB.CreateInBoundsGEP(NewAlloca, GEPIndices);
    auto *MallocStore = IRB.CreateStore(MallocCall, NewStructGEP);

    // Update all the users of the original struct to use the new struct
    for (auto *U : Users) {
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        // GEPs are handled separately to ensure that they are correctly typed
        updateStructGEP(NewAlloca, GEP);
        GEP->eraseFromParent();
      } else {
        U->replaceUsesOfWith(Alloca, NewAlloca);
      }
    }

    // Save the struct index of the newly-promoted array
    PromotedElemIndices.push_back(StructIndex);
  }

  return std::make_pair(NewAlloca, PromotedElemIndices);
}

bool PromoteStaticArrays::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();

  this->DL = new DataLayout(M.getDataLayout());
  this->IntPtrTy = this->DL->getIntPtrType(C);
  this->Int32Ty = Type::getInt32Ty(C);

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
    std::pair<AllocaInst *, std::vector<unsigned>> NewAllocaWithIndices =
        promoteStructAlloca(Alloca);
    Alloca->eraseFromParent();

    auto *NewAlloca = NewAllocaWithIndices.first;
    std::vector<unsigned> PromotedIndices = NewAllocaWithIndices.second;

    // The struct may have contained multiple static arrays that were promoted
    // to dynamically allocated arrays. Since the indices of these arrays (now
    // pointers) were recorded during promotion, we can iterate over these
    // pointers and free the memory that they point to.
    //
    // The index is used to create a GEP instruction that points to the
    // dynamically allocated array in the struct
    for (unsigned StructIndex : PromotedIndices) {
      for (auto *Return : ReturnsToInsertFree) {
        IRBuilder<> IRB(Return);

        // As per https://llvm.org/docs/GetElementPtr.html, struct member
        // indices always use i32
        Value *GEPIndices[] = {ConstantInt::get(this->Int32Ty, 0),
                               ConstantInt::get(this->Int32Ty, StructIndex)};
        auto *NewStructGEP = cast<GetElementPtrInst>(
            IRB.CreateInBoundsGEP(NewAlloca, GEPIndices));

        insertFree(NewStructGEP, Return);
      }
    }
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<PromoteStaticArrays>
    X("static-array-prom", "Promote static array to malloc calls", false,
      false);
