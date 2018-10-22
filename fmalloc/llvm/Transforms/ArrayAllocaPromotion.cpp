#include <vector>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "array-alloca-prom"

STATISTIC(NumOfAllocaPromotion, "Number of array alloca promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

class ArrayAllocaPromotion : public ModulePass {
private:
  Type *IntPtrTy;

  Value *updateGEP(AllocaInst *Alloca, GetElementPtrInst *GEP);
  Value *promoteArrayAlloca(const DataLayout &DL, AllocaInst *Alloca);
  void insertFree(Value *Alloca, ReturnInst *Return);

public:
  static char ID;
  ArrayAllocaPromotion() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char ArrayAllocaPromotion::ID = 0;

Value *ArrayAllocaPromotion::updateGEP(AllocaInst *Alloca,
                                       GetElementPtrInst *GEP) {
  std::vector<User *> Users(GEP->user_begin(), GEP->user_end());

  IRBuilder<> IRB(GEP);

  auto *LoadMalloc = IRB.CreateLoad(Alloca);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      LoadMalloc, std::vector<Value *>(GEP->idx_begin() + 1, GEP->idx_end()));

  for (auto *U : Users) {
    U->replaceUsesOfWith(GEP, NewGEP);
  }

  return NewGEP;
}

Value *ArrayAllocaPromotion::promoteArrayAlloca(const DataLayout &DL,
                                                AllocaInst *Alloca) {
  std::vector<User *> Users(Alloca->user_begin(), Alloca->user_end());

  Type *ArrayTy = Alloca->getAllocatedType();
  Type *ElemTy = ArrayTy->getArrayElementType();

  IRBuilder<> IRB(Alloca);

  auto *NewAlloca = IRB.CreateAlloca(ElemTy->getPointerTo());
  auto *MallocCall = CallInst::CreateMalloc(
      Alloca, this->IntPtrTy, ElemTy,
      ConstantInt::get(this->IntPtrTy, DL.getTypeAllocSize(ElemTy)),
      ConstantInt::get(this->IntPtrTy, ArrayTy->getArrayNumElements()),
      nullptr);
  auto *StoreMalloc = IRB.CreateStore(MallocCall, NewAlloca);

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

void ArrayAllocaPromotion::insertFree(Value *Alloca, ReturnInst *Return) {
    IRBuilder<> IRB(Return);

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
  std::vector<AllocaInst *> AllocasToPromote;
  std::vector<ReturnInst *> ReturnsToInsertFree;

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (isa<ArrayType>(Alloca->getAllocatedType())) {
          AllocasToPromote.push_back(Alloca);
          NumOfAllocaPromotion++;
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        ReturnsToInsertFree.push_back(Return);
        NumOfFreeInsert++;
      }
    }
  }

  for (auto *Alloca : AllocasToPromote) {
    auto *NewAlloca = promoteArrayAlloca(M.getDataLayout(), Alloca);
    Alloca->eraseFromParent();

    for (auto *Return : ReturnsToInsertFree) {
        insertFree(NewAlloca, Return);
    }
  }

  // TODO promote global variables

  return true;
}

static RegisterPass<ArrayAllocaPromotion>
    X("array-alloca-prom", "Promote array allocas to malloc calls", false,
      false);

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
