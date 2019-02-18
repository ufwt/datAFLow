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

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CaptureTracking.h"
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

struct PromotedStruct {
  const StructType *Ty;
  const std::map<ArrayType *, unsigned> ArrayElemIndices;

  PromotedStruct(StructType *Ty, std::map<ArrayType *, unsigned> ArrayElemIdxs)
      : Ty(Ty), ArrayElemIndices(ArrayElemIdxs) {}
};

/// PromoteStaticArrays: instrument the code in a module to promote static,
/// fixed-size arrays (both global and stack-based) to dynamically allocated
/// arrays via \p malloc.
class PromoteStaticArrays : public ModulePass {
private:
  using AllocaWithPromotedIndices = std::pair<AllocaInst *, ArrayRef<unsigned>>;

  DataLayout *DL;
  IntegerType *IntPtrTy;
  IntegerType *Int32Ty;

  std::map<StructType *, PromotedStruct> PromotedStructs;

  Instruction *createMalloc(Instruction *, Type *, uint64_t);

  Value *updateArrayGEP(GetElementPtrInst *, Instruction *);

  AllocaInst *promoteArrayAlloca(AllocaInst *);
  AllocaWithPromotedIndices promoteStructAlloca(AllocaInst *);

  Function *promoteFunction(Function *);

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
    if (auto *StructElem = dyn_cast<StructType>(Elem)) {
      return structContainsArray(StructElem);
    } else if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
      return true;
    }
  }

  return false;
}

/// Returns \p true if the given type contains an array.
///
/// If the type is a struct, each element in the struct is (recursively)
/// checked. If the type is a function, the return and parameter types are
/// checked.
static bool containsArray(const Type *Ty) {
  if (Ty->isArrayTy()) {
    return true;
  } else if (auto *Struct = dyn_cast<StructType>(Ty)) {
    if (std::any_of(Struct->element_begin(), Struct->element_end(),
                    &containsArray)) {
      return true;
    }
  } else if (auto *Func = dyn_cast<FunctionType>(Ty)) {
    if (containsArray(Func->getReturnType())) {
      return true;
    }

    if (std::any_of(Func->param_begin(), Func->param_end(), &containsArray)) {
      return true;
    }
  }

  return false;
}

static PromotedStruct promoteStruct(const StructType *StructTy) {
  // Maps the type of a static array (that will be promoted to a dynamically
  // allocated array) to its position in (i.e., the index of) the struct
  std::map<ArrayType *, unsigned> ArrayElemsWithIndices;

  // The elements of the new struct (i.e., with static arrays replaced by
  // pointers to dynamically allocated memory)
  SmallVector<Type *, 8> NewStructElems;

  // Record all of the static arrays (and their index/position in the struct)
  // that are to be promoted to dynamically allocated arrays
  {
    unsigned Index = 0;
    for (auto *Elem : StructTy->elements()) {
      if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
        ArrayElemsWithIndices.emplace(ArrayElem, Index);
        NewStructElems.push_back(
            ArrayElem->getArrayElementType()->getPointerTo());
      } else {
        NewStructElems.push_back(Elem);
      }

      Index++;
    }
  }

  // The new struct type (with static arrays replaced by pointers)
  StructType *NewStructTy = StructType::create(
      StructTy->getContext(), NewStructElems,
      StructTy->getName().str() + "_prom", StructTy->isPacked());

  return PromotedStruct(NewStructTy, ArrayElemsWithIndices);
}

static void insertFree(Instruction *MallocPtr, ReturnInst *Return) {
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

Value *PromoteStaticArrays::updateArrayGEP(GetElementPtrInst *GEP,
                                           Instruction *MallocPtr) {
  // Cache uses
  SmallVector<User *, 8> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction (based on the original static array GEP). The initial "offset
  // 0" that is used when accessing static arrays is ignored
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      Load, SmallVector<Value *, 4>(GEP->idx_begin() + 1, GEP->idx_end()),
      GEP->getName() + "_prom");

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  for (auto *U : Users) {
    U->replaceUsesOfWith(GEP, NewGEP);
  }

  return NewGEP;
}

AllocaInst *PromoteStaticArrays::promoteArrayAlloca(AllocaInst *Alloca) {
  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

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
  auto *NewAlloca = IRB.CreateAlloca(ElemTy->getPointerTo(), nullptr,
                                     Alloca->getName() + "_prom");
  auto *MallocCall =
      createMalloc(&*IRB.GetInsertPoint(), ElemTy, ArrayNumElems);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);

  // Update all the users of the original array to use the dynamically
  // allocated array
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // GEPs are handled separately to ensure that they are correctly typed
      updateArrayGEP(GEP, NewAlloca);
      GEP->eraseFromParent();
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  return NewAlloca;
}

// TODO generalise for nested structs
PromoteStaticArrays::AllocaWithPromotedIndices
PromoteStaticArrays::promoteStructAlloca(AllocaInst *Alloca) {
  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  // This is safe because we already know that the alloca has a struct type
  StructType *StructTy = cast<StructType>(Alloca->getAllocatedType());

  // Create a new type for the promoted struct if it does not already exist
  if (this->PromotedStructs.find(StructTy) == this->PromotedStructs.end()) {
    this->PromotedStructs.emplace(StructTy, promoteStruct(StructTy));
  }
  PromotedStruct PromStruct = this->PromotedStructs.find(StructTy)->second;

  IRBuilder<> IRB(Alloca);

  auto *NewAlloca = IRB.CreateAlloca(const_cast<StructType *>(PromStruct.Ty),
                                     nullptr, Alloca->getName() + "_prom");

  // Keep track of the position (i.e., index) of a promoted array in the struct
  SmallVector<unsigned, 8> PromotedElemIndices;

  // Promote each of the static arrays found in the struct to a dynamically
  // allocated array
  for (auto &ArrayElemIndices : PromStruct.ArrayElemIndices) {
    ArrayType *ArrayTy = ArrayElemIndices.first;
    unsigned ElemIndex = ArrayElemIndices.second;

    // As per https://llvm.org/docs/GetElementPtr.html, struct element indices
    // always use i32
    Value *GEPIndices[] = {ConstantInt::get(this->Int32Ty, 0),
                           ConstantInt::get(this->Int32Ty, ElemIndex)};

    Type *ElemTy = ArrayTy->getArrayElementType();
    uint64_t ArrayNumElems = ArrayTy->getArrayNumElements();

    auto *MallocCall =
        createMalloc(&*IRB.GetInsertPoint(), ElemTy, ArrayNumElems);
    auto *NewStructGEP = IRB.CreateInBoundsGEP(NewAlloca, GEPIndices);
    auto *MallocStore = IRB.CreateStore(MallocCall, NewStructGEP);

    // Save the element index of the newly-promoted array
    PromotedElemIndices.push_back(ElemIndex);
  }

  // Update all the users of the original struct to use the new struct
  for (auto *U : Users) {
    U->replaceUsesOfWith(Alloca, NewAlloca);
  }

  return std::make_pair(NewAlloca, PromotedElemIndices);
}

Function *PromoteStaticArrays::promoteFunction(Function *OrigF) {
  for (auto &Arg : OrigF->args()) {
    if (!containsArray(Arg.getType())) {
      continue;
    }

    // Cache uses
    SmallVector<User *, 8> Users(Arg.user_begin(), Arg.user_end());

    // This is safe because we already know that the argument has a struct type
    StructType *StructTy = cast<StructType>(Arg.getType());

    if (this->PromotedStructs.find(StructTy) == this->PromotedStructs.end()) {
      this->PromotedStructs.emplace(StructTy, promotedStruct(StructTy));
    }
    PromotedStruct PromStruct = this->PromotedStructs.find(StructTy)->second;

    // TODO finish this off
  }

  return OrigF;
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
  SmallVector<Function *, 8> FuncsToDelete;

  for (auto &F : M.functions()) {
    // List of static array allocations to promote
    SmallVector<AllocaInst *, 8> ArrayAllocasToPromote;

    // List of struct allocations that contain a static array to promote
    std::vector<AllocaInst *> StructAllocasToPromote;

    // List of return instructions that require calls to free to be inserted
    // before them
    SmallVector<ReturnInst *, 4> ReturnsToInsertFree;

    // Collect things to promote
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        Type *AllocaTy = Alloca->getAllocatedType();

        if (isa<ArrayType>(AllocaTy)) {
          bool AllocaEscapes = PointerMayBeCaptured(Alloca, false, true);
          // TODO do something with escape analysis result

          ArrayAllocasToPromote.push_back(Alloca);
          NumOfAllocaPromotion++;
        } else if (auto *StructTy = dyn_cast<StructType>(AllocaTy)) {
          if (containsArray(StructTy)) {
            bool AllocaEscapes = PointerMayBeCaptured(Alloca, false, true);
            // TODO do something with escape analysis result

            StructAllocasToPromote.push_back(Alloca);
            NumOfAllocaPromotion++;
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

    // Promote static arrays nested within structs to dynamically allocated
    // arrays
    for (auto *Alloca : StructAllocasToPromote) {
      AllocaWithPromotedIndices NewAllocaWithIndices =
          promoteStructAlloca(Alloca);

      auto *NewAlloca = NewAllocaWithIndices.first;
      ArrayRef<unsigned> PromotedIndices = NewAllocaWithIndices.second;

      // The struct may have contained multiple static arrays that were
      // promoted to dynamically allocated arrays. Since the indices of these
      // arrays (now pointers) were recorded during promotion, we can iterate
      // over these pointers and free the memory that they point to.
      //
      // The index is used to create a GEP instruction that points to the
      // dynamically allocated array in the struct
      for (unsigned StructIndex : PromotedIndices) {
        //        for (auto *Return : ReturnsToInsertFree) {
        //          IRBuilder<> IRB(Return);
        //
        //          // As per https://llvm.org/docs/GetElementPtr.html, struct
        //          member
        //          // indices always use i32
        //          Value *GEPIndices[] = {ConstantInt::get(this->Int32Ty, 0),
        //                                 ConstantInt::get(this->Int32Ty,
        //                                 StructIndex)};
        //          auto *NewStructGEP = cast<GetElementPtrInst>(
        //              IRB.CreateInBoundsGEP(NewAlloca, GEPIndices));
        //
        //          insertFree(NewStructGEP, Return);
        //        }
      }

      Alloca->eraseFromParent();
    }
  }

  // TODO promote global static arrays

  return true;
}

static RegisterPass<PromoteStaticArrays>
    X("static-array-prom", "Promote static array to malloc calls", false,
      false);
