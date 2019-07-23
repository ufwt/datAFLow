//===-- PromoteCommon.cpp - Promote static arrays to mallocs --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality for static array/struct promotion.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/Instructions.h"

#include "PromoteCommon.h"

using namespace llvm;

Value *updateGEP(GetElementPtrInst *GEP, Value *MallocPtr) {
  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction. It seems that the simplest way is to cast the loaded pointer
  // to the original array type
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *Bitcast = IRB.CreateBitCast(Load, GEP->getOperand(0)->getType());
  auto *NewGEP = IRB.CreateInBoundsGEP(
      Bitcast, SmallVector<Value *, 4>(GEP->idx_begin(), GEP->idx_end()),
      GEP->hasName() ? GEP->getName() + "_prom" : "");

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  GEP->replaceAllUsesWith(NewGEP);
  GEP->eraseFromParent();

  return NewGEP;
}

Value *updateSelect(SelectInst *Select, Value *OrigV, Value *NewV) {
  // The use of a promoted value in a select instruction may need to be cast (to
  // ensure that the select instruction type checks)

  // The original value must be one of the select values
  assert(Select->getTrueValue() == OrigV || Select->getFalseValue() == OrigV);

  auto *LoadNewV = new LoadInst(NewV, "", Select);
  auto *BitCastNewV =
      CastInst::CreatePointerCast(LoadNewV, Select->getType(), "", Select);
  Select->replaceUsesOfWith(OrigV, BitCastNewV);

  return Select;
}

bool isPromotableType(Type *Ty) {
  if (!Ty->isArrayTy()) {
    return false;
  }

  // Don't promote va_list (i.e., variable arguments): it's too hard and for
  // some reason everything breaks :(
  if (auto *StructTy = dyn_cast<StructType>(Ty->getArrayElementType())) {
    if (!StructTy->isLiteral() &&
        StructTy->getName().equals("struct.__va_list_tag")) {
      return false;
    }
  }

  return true;
}

Instruction *createArrayMalloc(LLVMContext &C, const DataLayout &DL,
                               IRBuilder<> &IRB, Type *AllocTy,
                               uint64_t ArrayNumElems, const Twine &Name) {
  IntegerType *IntPtrTy = DL.getIntPtrType(C);
  uint64_t TypeSize = DL.getTypeAllocSize(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), IntPtrTy, AllocTy,
                                ConstantInt::get(IntPtrTy, TypeSize),
                                ConstantInt::get(IntPtrTy, ArrayNumElems),
                                nullptr, Name);
}

void insertFree(Value *MallocPtr, Instruction *Inst) {
  // Load the pointer to the dynamically allocated memory and pass it to free
  auto *LoadMalloc = new LoadInst(MallocPtr, "", Inst);
  CallInst::CreateFree(LoadMalloc, Inst);
}
