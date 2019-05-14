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
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DIBuilder.h"
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
#include "debug.h" // from afl

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-prom-static-arrays"

static cl::opt<int> ClMinArraySize(
    "fuzzalloc-min-array-size",
    cl::desc("The minimum size of a static array to promote to malloc"),
    cl::init(1));

STATISTIC(NumOfAllocaArrayPromotion, "Number of alloca array promotions.");
STATISTIC(NumOfGlobalVariableArrayPromotion,
          "Number of global variable array promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteStaticArrays: instrument the code in a module to promote static,
/// fixed-size arrays (both global and stack-based) to dynamically allocated
/// arrays via \p malloc.
class PromoteStaticArrays : public ModulePass {
private:
  DataLayout *DL;
  DIBuilder *DBuilder;
  IntegerType *IntPtrTy;

  Instruction *createArrayMalloc(IRBuilder<> &, Type *, uint64_t) const;
  void insertMalloc(const AllocaInst *, AllocaInst *, Instruction *) const;
  void copyDebugInfo(const AllocaInst *, AllocaInst *) const;
  AllocaInst *promoteAlloca(AllocaInst *,
                            const ArrayRef<IntrinsicInst *> &) const;
  GlobalVariable *promoteGlobalVariable(GlobalVariable *, Function *) const;

public:
  static char ID;
  PromoteStaticArrays() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteStaticArrays::ID = 0;

/// Similar to `GetUnderlyingObject`, except that load instructions are followed
/// so that we can find the underlying alloca instruction.
static Value *getUnderlyingAlloca(Value *V, const DataLayout &DL) {
  auto *Obj = GetUnderlyingObject(V, DL);

  if (auto *Alloca = dyn_cast<AllocaInst>(Obj)) {
    return Alloca;
  } else if (auto *Load = dyn_cast<LoadInst>(Obj)) {
    return getUnderlyingAlloca(Load->getPointerOperand(), DL);
  } else {
    assert(false && "Failed to get underlying alloca");
  }
}

static bool isPromotableType(Type *Ty) {
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

/// Create a constructor function that will be used to `malloc` all of the
/// promoted global variables in the module.
static Function *createArrayPromCtor(Module &M) {
  LLVMContext &C = M.getContext();

  FunctionType *GlobalCtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__init_prom_global_arrays_" + M.getName(), &M);

  BasicBlock *GlobalCtorBB = BasicBlock::Create(C, "", GlobalCtorF);
  ReturnInst::Create(C, GlobalCtorBB);

  appendToGlobalCtors(M, GlobalCtorF, 0);

  return GlobalCtorF;
}

/// Create a destructor function that will be used to `free` all of the
/// promoted global variables in the module.
static Function *createArrayPromDtor(Module &M) {
  LLVMContext &C = M.getContext();

  FunctionType *GlobalDtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalDtorF =
      Function::Create(GlobalDtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__fin_prom_global_arrays_" + M.getName(), &M);

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
  Load->setMetadata(GEP->getModule()->getMDKindID("nosanitize"),
                    MDNode::get(GEP->getContext(), None));

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

Instruction *
PromoteStaticArrays::createArrayMalloc(IRBuilder<> &IRB, Type *AllocTy,
                                       uint64_t ArrayNumElems) const {
  uint64_t TypeSize = this->DL->getTypeAllocSize(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), this->IntPtrTy, AllocTy,
                                ConstantInt::get(this->IntPtrTy, TypeSize),
                                ConstantInt::get(this->IntPtrTy, ArrayNumElems),
                                nullptr);
}

/// Insert a call to `malloc` before the `InsertPt` instruction. The result of
/// the `malloc` call is stored in `NewAlloca`.
void PromoteStaticArrays::insertMalloc(const AllocaInst *OrigAlloca,
                                       AllocaInst *NewAlloca,
                                       Instruction *InsertPt) const {
  const Module *M = OrigAlloca->getModule();
  LLVMContext &C = M->getContext();

  ArrayType *ArrayTy = cast<ArrayType>(OrigAlloca->getAllocatedType());
  Type *ElemTy = ArrayTy->getArrayElementType();

  uint64_t ArrayNumElems = ArrayTy->getNumElements();

  IRBuilder<> IRB(InsertPt);

  auto *MallocCall = createArrayMalloc(IRB, ElemTy, ArrayNumElems);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                           MDNode::get(C, None));
  MallocStore->setMetadata(M->getMDKindID("nosanitize"), MDNode::get(C, None));
}

void PromoteStaticArrays::copyDebugInfo(const AllocaInst *OrigAlloca,
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

AllocaInst *PromoteStaticArrays::promoteAlloca(
    AllocaInst *Alloca, const ArrayRef<IntrinsicInst *> &LifetimeStarts) const {
  LLVM_DEBUG(dbgs() << "promoting" << *Alloca << " in function "
                    << Alloca->getFunction()->getName() << ")\n");

  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  const Module *M = Alloca->getModule();
  LLVMContext &C = M->getContext();

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
      if (getUnderlyingAlloca(LifetimeStart->getOperand(1), *this->DL) ==
          Alloca) {
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
      GEP->eraseFromParent();
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
      // We must load the array from the heap before we do anything with it

      auto *LoadNewAlloca = new LoadInst(NewAlloca, "", cast<Instruction>(U));
      LoadNewAlloca->setMetadata(M->getMDKindID("nosanitize"),
                                 MDNode::get(C, None));
      U->replaceUsesOfWith(Alloca, LoadNewAlloca);
    }
  }

  return NewAlloca;
}

GlobalVariable *
PromoteStaticArrays::promoteGlobalVariable(GlobalVariable *OrigGV,
                                           Function *ArrayPromCtor) const {
  LLVM_DEBUG(dbgs() << "promoting " << *OrigGV << '\n');

  Module *M = OrigGV->getParent();
  LLVMContext &C = M->getContext();

  // Insert a new global variable into the module and initialize it with a call
  // to malloc in the module's constructor
  IRBuilder<> IRB(ArrayPromCtor->getEntryBlock().getTerminator());

  ArrayType *ArrayTy = cast<ArrayType>(OrigGV->getValueType());
  Type *ElemTy = ArrayTy->getArrayElementType();
  uint64_t ArrayNumElems = ArrayTy->getNumElements();
  PointerType *NewGVTy = ElemTy->getPointerTo();

  GlobalVariable *NewGV = new GlobalVariable(
      *M, NewGVTy, false, OrigGV->getLinkage(),
      // If the original global variable had an initializer, replace it with the
      // null pointer initializer
      !OrigGV->isDeclaration() ? Constant::getNullValue(NewGVTy) : nullptr,
      OrigGV->getName() + "_prom", nullptr, OrigGV->getThreadLocalMode(),
      OrigGV->getType()->getAddressSpace(), OrigGV->isExternallyInitialized());
  NewGV->copyAttributesFrom(OrigGV);

  // Copy debug info
  SmallVector<DIGlobalVariableExpression *, 1> GVs;
  OrigGV->getDebugInfo(GVs);
  for (auto *GV : GVs) {
    NewGV->addDebugInfo(GV);
  }

  auto *MallocCall = createArrayMalloc(IRB, ElemTy, ArrayNumElems);

  // If the array had an initializer, we must replicate it so that the malloc'd
  // memory contains the same data when it is first used. How we do this depends
  // on the initializer
  if (OrigGV->hasInitializer()) {
    if (auto *Initializer =
            dyn_cast<ConstantDataArray>(OrigGV->getInitializer())) {
      // If the initializer is a constant data array, we store the data into the
      // dynamically allocated array element-by-element. Hopefully the backend
      // is smart enough to generate efficient code for this...
      unsigned NumElems = Initializer->getNumElements();

      for (unsigned i = 0; i < NumElems; ++i) {
        auto *StoreToNewGV = IRB.CreateStore(
            Initializer->getElementAsConstant(i),
            IRB.CreateInBoundsGEP(MallocCall,
                                  ConstantInt::get(ElemTy, i, false)));
        StoreToNewGV->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                                  MDNode::get(C, None));
        StoreToNewGV->setMetadata(M->getMDKindID("nosanitize"),
                                  MDNode::get(C, None));
      }
    } else if (isa<ConstantAggregateZero>(OrigGV->getInitializer())) {
      // If the initializer is the zeroinitialier, just memset the dynamically
      // allocated memory to zero
      uint64_t Size = this->DL->getTypeAllocSize(ElemTy) * ArrayNumElems;
      IRB.CreateMemSet(MallocCall, Constant::getNullValue(IRB.getInt8Ty()),
                       Size, OrigGV->getAlignment());
    } else {
      assert(false && "Unsupported initializer type");
    }
  }

  auto *MallocStore = IRB.CreateStore(MallocCall, NewGV);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                           MDNode::get(C, None));
  MallocStore->setMetadata(M->getMDKindID("nosanitize"), MDNode::get(C, None));

  // Now that the global variable has been promoted to the heap, it must be
  // loaded before we can do anything else to it. This means that any constant
  // expressions that used the old global variable must be replaced, because a
  // load instruction is not a constant expression. To do this we just expand
  // all constant expression users to instructions.
  SmallVector<ConstantExpr *, 4> CEUsers;
  for (auto *U : OrigGV->users()) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      CEUsers.push_back(CE);
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
    } else if (auto *Inst = dyn_cast<Instruction>(U)) {
      auto *LoadNewGV = new LoadInst(NewGV, "", Inst);
      LoadNewGV->setMetadata(M->getMDKindID("nosanitize"),
                             MDNode::get(C, None));
      U->replaceUsesOfWith(OrigGV, LoadNewGV);
    } else {
      assert(false && "Unsupported user");
    }
  }

  return NewGV;
}

bool PromoteStaticArrays::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();

  this->DL = new DataLayout(M.getDataLayout());
  this->DBuilder = new DIBuilder(M);
  this->IntPtrTy = this->DL->getIntPtrType(C);

  return false;
}

bool PromoteStaticArrays::doFinalization(Module &) {
  delete this->DL;

  this->DBuilder->finalize();
  delete this->DBuilder;

  return false;
}

bool PromoteStaticArrays::runOnModule(Module &M) {
  // Static array allocations to promote
  SmallVector<AllocaInst *, 8> AllocasToPromote;

  // lifetime.start intrinsics that will require calls to mallic to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeStarts;

  // lifetime.end intrinsics that will require calls to free to be inserted
  // before them
  SmallVector<IntrinsicInst *, 4> LifetimeEnds;

  // Return instructions that may require calls to free to be inserted before
  // them
  SmallVector<ReturnInst *, 4> Returns;

  for (auto &F : M.functions()) {
    AllocasToPromote.clear();
    LifetimeStarts.clear();
    LifetimeEnds.clear();
    Returns.clear();

    // Collect all the things!
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (isPromotableType(Alloca->getAllocatedType())) {
          AllocasToPromote.push_back(Alloca);
        }
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
          if (getUnderlyingAlloca(LifetimeEnd->getOperand(1), *this->DL) ==
              NewAlloca) {
            insertFree(NewAlloca, LifetimeEnd);
            NumOfFreeInsert++;
          }
        }
      }

      Alloca->eraseFromParent();
      NumOfAllocaArrayPromotion++;
    }
  }

  // Promote non-constant global static arrays in a module constructor and free
  // them in a destructor
  SmallVector<GlobalVariable *, 8> GlobalVariablesToPromote;

  for (auto &GV : M.globals()) {
    if (isPromotableType(GV.getValueType()) && !GV.isConstant()) {
      // For some reason this doesn't mark the global variable as constant
      if (GV.hasInitializer() && isa<ConstantArray>(GV.getInitializer())) {
        continue;
      }

      GlobalVariablesToPromote.push_back(&GV);
    }
  }

  if (!GlobalVariablesToPromote.empty()) {
    Function *GlobalCtorF = createArrayPromCtor(M);
    Function *GlobalDtorF = createArrayPromDtor(M);

    for (auto *GV : GlobalVariablesToPromote) {
      auto *PromotedGV = promoteGlobalVariable(GV, GlobalCtorF);
      NumOfGlobalVariableArrayPromotion++;

      if (!PromotedGV->isDeclaration()) {
        insertFree(PromotedGV, GlobalDtorF->getEntryBlock().getTerminator());
        NumOfFreeInsert++;
      }

      GV->eraseFromParent();
    }
  }

  if (NumOfAllocaArrayPromotion > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfAllocaArrayPromotion.getValue(),
        NumOfAllocaArrayPromotion.getName(),
        NumOfAllocaArrayPromotion.getDesc());
  }
  if (NumOfGlobalVariableArrayPromotion > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfGlobalVariableArrayPromotion.getValue(),
        NumOfGlobalVariableArrayPromotion.getName(),
        NumOfGlobalVariableArrayPromotion.getDesc());
  }

  return (NumOfAllocaArrayPromotion > 0) ||
         (NumOfGlobalVariableArrayPromotion > 0);
}

static RegisterPass<PromoteStaticArrays>
    X("fuzzalloc-prom-static-arrays", "Promote static arrays to malloc calls",
      false, false);

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
