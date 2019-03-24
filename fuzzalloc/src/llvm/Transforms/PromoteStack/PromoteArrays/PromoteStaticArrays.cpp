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
#include "llvm/Transforms/Utils/ModuleUtils.h"

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
  AllocaInst *promoteArrayAlloca(AllocaInst *);
  GlobalVariable *promoteGlobalVariable(GlobalVariable *, Function *);

public:
  static char ID;
  PromoteStaticArrays() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteStaticArrays::ID = 0;

static Function *createArrayPromCtor(Module &M) {
  LLVMContext &C = M.getContext();

  FunctionType *GlobalCtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__init_prom_global_arrays", &M);

  BasicBlock *GlobalCtorBB = BasicBlock::Create(C, "", GlobalCtorF);
  ReturnInst::Create(C, GlobalCtorBB);

  appendToGlobalCtors(M, GlobalCtorF, 0);

  return GlobalCtorF;
}

static Function *createArrayPromDtor(Module &M) {
  LLVMContext &C = M.getContext();

  FunctionType *GlobalDtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalDtorF =
      Function::Create(GlobalDtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__fin_prom_global_arrays", &M);

  BasicBlock *GlobalDtorBB = BasicBlock::Create(C, "", GlobalDtorF);
  ReturnInst::Create(C, GlobalDtorBB);

  appendToGlobalDtors(M, GlobalDtorF, 0);

  return GlobalDtorF;
}

static Value *updateGEP(GetElementPtrInst *GEP, Value *MallocPtr) {
  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction. Static arrays use an initial "offset 0" that must be ignored
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      Load, SmallVector<Value *, 4>(GEP->idx_begin() + 1, GEP->idx_end()),
      GEP->hasName() ? GEP->getName() + "_prom" : "");

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  GEP->replaceAllUsesWith(NewGEP);

  return NewGEP;
}

static void expandConstantExpression(ConstantExpr *ConstExpr) {
  for (auto *U : ConstExpr->users()) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      expandConstantExpression(CE);
    }
  }

  SmallVector<User *, 4> Users(ConstExpr->user_begin(), ConstExpr->user_end());

  // At this pont, all of the users must be instructions. We can just insert a
  // new instruction representing the constant expression before each user
  for (auto *U : Users) {
    if (auto *PHI = dyn_cast<PHINode>(U)) {
      // PHI nodes are handled differently because they must always be the first
      // instruction in a basic block. To ensure this property is true we must
      // insert the new instruction at the end of the appropriate predecessor
      // block(s)
      for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
        if (PHI->getIncomingValue(i) == ConstExpr) {
          Instruction *NewInst = ConstExpr->getAsInstruction();

          NewInst->insertBefore(PHI->getIncomingBlock(i)->getTerminator());
          PHI->setIncomingValue(i, NewInst);
        }
      }
    } else {
      Instruction *NewInst = ConstExpr->getAsInstruction();

      NewInst->insertBefore(cast<Instruction>(U));
      U->replaceUsesOfWith(ConstExpr, NewInst);
    }
  }

  ConstExpr->destroyConstant();
}

Instruction *PromoteStaticArrays::createArrayMalloc(IRBuilder<> &IRB,
                                                    Type *AllocTy,
                                                    uint64_t ArrayNumElems) {
  uint64_t TypeSize = this->DL->getTypeAllocSize(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), this->IntPtrTy, AllocTy,
                                ConstantInt::get(this->IntPtrTy, TypeSize),
                                ConstantInt::get(this->IntPtrTy, ArrayNumElems),
                                nullptr);
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

GlobalVariable *
PromoteStaticArrays::promoteGlobalVariable(GlobalVariable *OrigGV,
                                           Function *ArrayPromCtor) {
  LLVM_DEBUG(dbgs() << "promoting " << *OrigGV << '\n');

  // Insert a new global variable into the module and initialize it with a call
  // to malloc in the module's constructor
  IRBuilder<> IRB(ArrayPromCtor->getEntryBlock().getTerminator());

  ArrayType *ArrayTy = cast<ArrayType>(OrigGV->getValueType());
  Type *ElemTy = ArrayTy->getArrayElementType();
  uint64_t ArrayNumElems = ArrayTy->getNumElements();
  PointerType *NewGVTy = ElemTy->getPointerTo();

  GlobalVariable *NewGV = new GlobalVariable(
      *ArrayPromCtor->getParent(), NewGVTy, false, OrigGV->getLinkage(),
      // If the original global variable had an initializer, replace it with the
      // null pointer initializer
      !OrigGV->isDeclaration() ? Constant::getNullValue(NewGVTy) : nullptr,
      OrigGV->getName() + "_prom", nullptr, OrigGV->getThreadLocalMode(),
      OrigGV->getType()->getAddressSpace(), OrigGV->isExternallyInitialized());
  NewGV->copyAttributesFrom(OrigGV);

  auto *MallocCall = createArrayMalloc(IRB, ElemTy, ArrayNumElems);
  IRB.CreateStore(MallocCall, NewGV);

  // If the array had an initializer, we must replicate it so that the malloc'd
  // memory contains the same data when it is first used. How we do this depends
  // on the initializer
  if (OrigGV->hasInitializer()) {
    auto *LoadNewGV = IRB.CreateLoad(NewGV);

    if (auto *Initializer =
            dyn_cast<ConstantDataArray>(OrigGV->getInitializer())) {
      // If the initializer is a constant data array, we store the data into the
      // dynamically allocated array element-by-element. Hopefully the backend
      // is smart enough to generate efficient code for this...
      unsigned NumElems = Initializer->getNumElements();

      for (unsigned i = 0; i < NumElems; ++i) {
        IRB.CreateStore(Initializer->getElementAsConstant(i),
                        IRB.CreateInBoundsGEP(
                            LoadNewGV, ConstantInt::get(ElemTy, i, false)));
      }
    } else if (auto *Initializer =
                   dyn_cast<ConstantAggregateZero>(OrigGV->getInitializer())) {
      // If the initializer is the zeroinitialier, just memset the dynamically
      // allocated memory to zero
      uint64_t Size = this->DL->getTypeAllocSize(ElemTy) * ArrayNumElems;
      IRB.CreateMemSet(LoadNewGV, Constant::getNullValue(IRB.getInt8Ty()), Size,
                       OrigGV->getAlignment());
    } else {
      assert(false && "Unsupported initializer type");
    }
  }

  // If the global variable is used by any constant GEP expressions, we must
  // expand the constant expression (and any constant expression user) to an
  // instruction so that we can correctly replace the global variable with a
  // dynamically allocated version.
  //
  // This is required because we must first load the dynamically allocated
  // global variable before we use it in a GEP, which means the GEP is no longer
  // a constant expression
  SmallVector<ConstantExpr *, 4> CEUsers;
  for (auto *U : OrigGV->users()) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->getOpcode() == Instruction::GetElementPtr) {
        CEUsers.push_back(CE);
      }
    }
  }

  std::for_each(CEUsers.begin(), CEUsers.end(), expandConstantExpression);

  // Update all the users of the original global variable to use the
  // dynamically allocated array
  SmallVector<User *, 8> Users(OrigGV->user_begin(), OrigGV->user_end());

  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewGV);
      GEP->eraseFromParent();
    } else if (auto *ConstExpr = dyn_cast<ConstantExpr>(U)) {
      // All of the GEP constant expressions should have been expanded to
      // instructions by this point
      assert(ConstExpr->getOpcode() != Instruction::GetElementPtr);

      // This is equivalent to replaceUsesOfWith for constants
      ConstExpr->handleOperandChange(OrigGV, NewGV);
    } else {
      U->replaceUsesOfWith(OrigGV, NewGV);
    }
  }

  return NewGV;
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

  // Promote non-constant global static arrays in a module constructor and free
  // them in a destructor
  SmallVector<GlobalVariable *, 8> GlobalVariablesToPromote;

  for (auto &GV : M.globals()) {
    if (isa<ArrayType>(GV.getValueType()) && !GV.isConstant()) {
      GlobalVariablesToPromote.push_back(&GV);
    }
  }

  if (!GlobalVariablesToPromote.empty()) {
    Function *GlobalCtorF = createArrayPromCtor(M);
    Function *GlobalDtorF = createArrayPromDtor(M);

    for (auto *GV : GlobalVariablesToPromote) {
      auto *PromotedGV = promoteGlobalVariable(GV, GlobalCtorF);
      insertFree(PromotedGV, GlobalDtorF->getEntryBlock().getTerminator());

      GV->eraseFromParent();
      NumOfArrayPromotion++;
    }
  }

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
