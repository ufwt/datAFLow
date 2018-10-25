//===-- ArrayAllocaPromotion.cpp - Promote static arrays to mallocs -------===//
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
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define ARRAY_ALLOCA_PROM "array-alloca-prom"
#define DEBUG_TYPE ARRAY_ALLOCA_PROM

static cl::opt<int> ClMinArraySize(
    "array-alloca-prom-min-size",
    cl::desc("The minimum size of an array to promote to malloc"),
    cl::init(-1));

STATISTIC(NumOfAllocaPromotion, "Number of array alloca promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// ArrayAllocaPromotion: instrument the code in a module to promote static,
/// fixed-size arrays (both global and stack-based) to dynamically allocated
/// arrays via \p malloc.
class ArrayAllocaPromotion : public ModulePass {
private:
  Type *IntPtrTy;

  Value *updateGEP(AllocaInst *Alloca, GetElementPtrInst *GEP);
  AllocaInst *promoteArrayAlloca(const DataLayout &DL, AllocaInst *Alloca);
  AllocaInst *promoteStructAlloca(const DataLayout &DL, AllocaInst *Alloca);
  void insertFree(Instruction *Alloca, ReturnInst *Return);

public:
  static char ID;
  ArrayAllocaPromotion() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
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

char ArrayAllocaPromotion::ID = 0;

Value *ArrayAllocaPromotion::updateGEP(AllocaInst *Alloca,
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

AllocaInst *ArrayAllocaPromotion::promoteArrayAlloca(const DataLayout &DL,
                                                     AllocaInst *Alloca) {
  // Cache uses before creating more
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  Type *ArrayTy = Alloca->getAllocatedType();
  Type *ElemTy = ArrayTy->getArrayElementType();

  IRBuilder<> IRB(Alloca);

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
      ConstantInt::get(this->IntPtrTy, DL.getTypeAllocSize(ElemTy)),
      ConstantInt::get(this->IntPtrTy, ArrayTy->getArrayNumElements()),
      nullptr);
  auto *StoreMalloc = IRB.CreateStore(MallocCall, NewAlloca);

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

  return NewAlloca;
}

// TODO handle nested structs
AllocaInst *ArrayAllocaPromotion::promoteStructAlloca(const DataLayout &DL,
                                                      AllocaInst *Alloca) {
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

  // The new struct type (without any static arrays)
  LLVMContext &C = StructTy->getContext();
  StructType *NewStructTy = StructType::create(
      C, NewStructElements, StructTy->getName(), StructTy->isPacked());

  IntegerType *Int32Ty = Type::getInt32Ty(C);

  IRBuilder<> IRB(Alloca);

  auto *NewAlloca = IRB.CreateAlloca(NewStructTy);

  for (auto &ArrayTysWithIndex : StructArrayElements) {
    ArrayType *ArrayTy = ArrayTysWithIndex.first;
    unsigned Index = ArrayTysWithIndex.second;
    Type *ElemTy = ArrayTy->getArrayElementType();
    Value *GEPIndices[] = {ConstantInt::get(Int32Ty, 0),
                           ConstantInt::get(Int32Ty, Index)};

    auto *MallocCall = CallInst::CreateMalloc(
        Alloca, this->IntPtrTy, ElemTy,
        ConstantInt::get(this->IntPtrTy, DL.getTypeAllocSize(ElemTy)),
        ConstantInt::get(this->IntPtrTy, ArrayTy->getArrayNumElements()),
        nullptr);
    auto *NewStructGEP = IRB.CreateGEP(NewAlloca, GEPIndices);
    auto *StoreMalloc = IRB.CreateStore(MallocCall, NewStructGEP);

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

  return NewAlloca;
}

void ArrayAllocaPromotion::insertFree(Instruction *Alloca, ReturnInst *Return) {
  IRBuilder<> IRB(Return);

  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = IRB.CreateLoad(Alloca);
  CallInst::CreateFree(LoadMalloc, Return);
}

bool ArrayAllocaPromotion::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  this->IntPtrTy = DL.getIntPtrType(C);

  return true;
}

bool ArrayAllocaPromotion::runOnModule(Module &M) {
  std::vector<AllocaInst *> ArrayAllocasToPromote;
  std::vector<AllocaInst *> StructAllocasToPromote;
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
    auto *NewAlloca = promoteArrayAlloca(M.getDataLayout(), Alloca);
    NewAlloca->setMetadata(C.getMDKindID(ARRAY_ALLOCA_PROM),
                           MDNode::get(C, None));
    Alloca->eraseFromParent();

    // Ensure that the promoted alloca (now dynamically allocated) is freed
    // when the function returns
    for (auto *Return : ReturnsToInsertFree) {
      insertFree(NewAlloca, Return);
    }
  }

  for (auto *Alloca : StructAllocasToPromote) {
    auto *NewAlloca = promoteStructAlloca(M.getDataLayout(), Alloca);
    NewAlloca->setMetadata(C.getMDKindID(ARRAY_ALLOCA_PROM),
                           MDNode::get(C, None));
    // TODO erase Alloca

    // TODO insert frees
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<ArrayAllocaPromotion>
    X(ARRAY_ALLOCA_PROM, "Promote array allocas to malloc calls", false, false);

static void registerArrayAllocaPromotionPass(const PassManagerBuilder &,
                                             legacy::PassManagerBase &PM) {
  PM.add(new ArrayAllocaPromotion());
}

static RegisterStandardPasses
    RegisterArrayAllocaPromotionPass(PassManagerBuilder::EP_OptimizerLast,
                                     registerArrayAllocaPromotionPass);

static RegisterStandardPasses
    RegisterArrayAllocaPromotionPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                      registerArrayAllocaPromotionPass);
