//===-- HeapifyGlobalVariables.cpp - Heapify global variable arrays -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass heapifies static global variable arrays to dynamically-allocated
/// arrays via \p malloc.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "Common.h"
#include "HeapifyCommon.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-heapify-global-vars"

static cl::opt<int>
    ClMinArraySize("fuzzalloc-min-global-array-size",
                   cl::desc("The minimum size of a static global variable "
                            "array to heapify to malloc"),
                   cl::init(1));

STATISTIC(NumOfGlobalVariableArrayHeapification,
          "Number of global variable array heapifications.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// HeapifyGlobalVariables: instrument the code in a module to heapify static,
/// fixed-size global variable arrays to dynamically-allocated arrays via
/// \p malloc.
class HeapifyGlobalVariables : public ModulePass {
private:
  void initializeHeapifiedGlobalVariable(const GlobalVariable *,
                                         GlobalVariable *);
  void expandConstantExpression(ConstantExpr *);
  GlobalVariable *heapifyGlobalVariable(GlobalVariable *);

public:
  static char ID;
  HeapifyGlobalVariables() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char HeapifyGlobalVariables::ID = 0;

/// Create a constructor function that will be used to `malloc` the given
/// heapified global variable.
static IRBuilder<> createHeapifyCtor(GlobalVariable *GV) {
  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();

  FunctionType *GlobalCtorTy =
      FunctionType::get(Type::getVoidTy(C), /* isVarArg */ false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "fuzzalloc.alloc_" + GV->getName(), M);
  appendToGlobalCtors(*M, GlobalCtorF, kHeapifyGVCtorAndDtorPriority);

  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", GlobalCtorF);
  IRBuilder<> IRB(EntryBB);

  if (GV->getLinkage() == GlobalValue::LinkageTypes::LinkOnceAnyLinkage ||
      GV->getLinkage() == GlobalValue::LinkageTypes::LinkOnceODRLinkage) {
    // Weak linkage means that the same constructor may be inserted in multiple
    // modules, causing the global variable to be malloc'd multiple times. To
    // prevent this, we generate code to check if the global variable has
    // already been malloc'd. If so, just return.

    // Basic block when the global variable has already been allocated. Nothing
    // to do when this is the case
    BasicBlock *AllocTrueBB = BasicBlock::Create(C, "alloc.true", GlobalCtorF);
    ReturnInst::Create(C, AllocTrueBB);

    // Basic block when the global variable has not been allocated
    BasicBlock *AllocFalseBB =
        BasicBlock::Create(C, "alloc.false", GlobalCtorF);
    ReturnInst::Create(C, AllocFalseBB);

    // Load the global variable
    auto *LoadGV = IRB.CreateLoad(GV);
    LoadGV->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                        MDNode::get(C, None));

    // Check if the global variable has already been allocated
    auto *AllocCheck =
        IRB.CreateICmpNE(LoadGV, Constant::getNullValue(LoadGV->getType()));
    IRB.CreateCondBr(AllocCheck, AllocTrueBB, AllocFalseBB);

    // Only insert code when the global variable has not already been allocated
    IRB.SetInsertPoint(AllocFalseBB->getTerminator());
  } else {
    // No branching - just return from the entry block
    auto *RetVoid = IRB.CreateRetVoid();
    IRB.SetInsertPoint(RetVoid);
  }

  return IRB;
}

/// Create a destructor function that will be used to `free` all the given
/// heapified global variable.
static IRBuilder<> createHeapifyDtor(GlobalVariable *GV) {
  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();

  FunctionType *GlobalDtorTy =
      FunctionType::get(Type::getVoidTy(C), /* isVarArg */ false);
  Function *GlobalDtorF =
      Function::Create(GlobalDtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "fuzzalloc.free_" + GV->getName(), M);
  appendToGlobalDtors(*M, GlobalDtorF, kHeapifyGVCtorAndDtorPriority);

  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", GlobalDtorF);
  IRBuilder<> IRB(EntryBB);

  if (GV->getLinkage() == GlobalValue::LinkageTypes::LinkOnceAnyLinkage ||
      GV->getLinkage() == GlobalValue::LinkageTypes::LinkOnceODRLinkage) {
    // Weak linkage means that the same destructor may be inserted in multiple
    // modules, causing the global variable to be free'd multiple times. To
    // prevent this, we generate code to check if the global variable has
    // already been free'd. If so, just return.

    // Basic block when the global variable has already been freed. Nothing to
    // do when this is the case
    BasicBlock *FreeTrueBB = BasicBlock::Create(C, "free.true", GlobalDtorF);
    ReturnInst::Create(C, FreeTrueBB);

    // Basic block when the global variable has not been freed
    BasicBlock *FreeFalseBB = BasicBlock::Create(C, "free.false", GlobalDtorF);
    ReturnInst::Create(C, FreeFalseBB);

    // Load the global variable
    auto *LoadGV = IRB.CreateLoad(GV);
    LoadGV->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                        MDNode::get(C, None));

    // Check if the global variable has already been freed
    auto *FreeCheck =
        IRB.CreateICmpEQ(LoadGV, Constant::getNullValue(LoadGV->getType()));
    IRB.CreateCondBr(FreeCheck, FreeTrueBB, FreeFalseBB);

    // Set the global variable to NULL
    IRB.SetInsertPoint(FreeFalseBB->getTerminator());
    auto *NullStore =
        IRB.CreateStore(Constant::getNullValue(GV->getType()), GV);

    // Free the global variable before setting it to NULL
    IRB.SetInsertPoint(NullStore);
  } else {
    // No branching - just return from the entry block
    auto *RetVoid = IRB.CreateRetVoid();
    IRB.SetInsertPoint(RetVoid);
  }

  return IRB;
}

/// Initialize the heapified global variable in the given constructor function.
///
/// The initialization is based off the original global variable's static
/// initializer.
void HeapifyGlobalVariables::initializeHeapifiedGlobalVariable(
    const GlobalVariable *OrigGV, GlobalVariable *NewGV) {
  LLVM_DEBUG(dbgs() << "creating initializer for " << *NewGV << '\n');

  ArrayType *ArrayTy = cast<ArrayType>(OrigGV->getValueType());
  Type *ElemTy = ArrayTy->getArrayElementType();
  uint64_t ArrayNumElems = ArrayTy->getNumElements();

  // Insert a new global variable into the module and initialize it with a call
  // to malloc in a constructor
  IRBuilder<> IRB = createHeapifyCtor(NewGV);

  Module *M = NewGV->getParent();
  LLVMContext &C = M->getContext();
  const DataLayout &DL = M->getDataLayout();

  auto *MallocCall = createArrayMalloc(C, DL, IRB, ElemTy, ArrayNumElems,
                                       OrigGV->getName() + "_malloccall");

  // If the array had an initializer, we must replicate it so that the malloc'd
  // memory contains the same data when it is first used. How we do this depends
  // on the initializer
  if (OrigGV->hasInitializer()) {
    if (isa<ConstantAggregateZero>(OrigGV->getInitializer())) {
      // If the initializer is the zeroinitializer, just memset the dynamically
      // allocated memory to zero. Likewise with heapified allocas that are
      // memset, reset the destination alignment
      uint64_t Size = DL.getTypeAllocSize(ElemTy) * ArrayNumElems;
      IRB.CreateMemSet(MallocCall, Constant::getNullValue(IRB.getInt8Ty()),
                       Size, NewGV->getAlignment());
    } else if (auto *Initializer =
                   dyn_cast<ConstantDataArray>(OrigGV->getInitializer())) {
      // If the initializer is a constant data array, we store the data into the
      // dynamically allocated array element-by-element. Hopefully the optimizer
      // can improve this code
      for (unsigned I = 0; I < Initializer->getNumElements(); ++I) {
        auto *StoreToNewGV = IRB.CreateStore(
            Initializer->getElementAsConstant(I),
            IRB.CreateConstInBoundsGEP1_32(nullptr, MallocCall, I));
        StoreToNewGV->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                                  MDNode::get(C, None));
      }
    } else if (isa<ConstantArray>(OrigGV->getInitializer())) {
      assert(false && "Constant array initializers should already be expanded");
    } else {
      assert(false && "Unsupported global variable initializer");
    }
  }

  auto *MallocStore = IRB.CreateStore(MallocCall, NewGV);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                           MDNode::get(C, None));
}

void HeapifyGlobalVariables::expandConstantExpression(ConstantExpr *ConstExpr) {
  for (auto *U : ConstExpr->users()) {
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      expandConstantExpression(CE);
    }
  }

  // Cache users
  SmallVector<User *, 4> Users(ConstExpr->user_begin(), ConstExpr->user_end());

  // At this point, all of the users must be instructions. We can just insert a
  // new instruction representing the constant expression before each user
  for (auto *U : Users) {
    if (auto *PHI = dyn_cast<PHINode>(U)) {
      // PHI nodes are a special case because they must always be the first
      // instruction in a basic block. To ensure this property is true we must
      // insert the new instruction at the end of the appropriate predecessor
      // block(s)
      for (unsigned I = 0; I < PHI->getNumIncomingValues(); ++I) {
        if (PHI->getIncomingValue(I) == ConstExpr) {
          Instruction *NewInst = ConstExpr->getAsInstruction();

          NewInst->insertBefore(PHI->getIncomingBlock(I)->getTerminator());
          PHI->setIncomingValue(I, NewInst);
        }
      }
    } else if (auto *Inst = dyn_cast<Instruction>(U)) {
      Instruction *NewInst = ConstExpr->getAsInstruction();
      NewInst->insertBefore(Inst);
      Inst->replaceUsesOfWith(ConstExpr, NewInst);
    } else if (auto *Const = dyn_cast<Constant>(U)) {
      Const->removeDeadConstantUsers();
      assert(Const->hasNUses(0));
    } else {
      assert(false && "Unsupported constant expression user");
    }
  }
}

GlobalVariable *
HeapifyGlobalVariables::heapifyGlobalVariable(GlobalVariable *OrigGV) {
  LLVM_DEBUG(dbgs() << "heapifying " << *OrigGV << '\n');

  Module *M = OrigGV->getParent();
  ArrayType *ArrayTy = cast<ArrayType>(OrigGV->getValueType());
  PointerType *NewGVTy = ArrayTy->getArrayElementType()->getPointerTo();

  GlobalVariable *NewGV = new GlobalVariable(
      *M, NewGVTy, /* isConstant */ false, OrigGV->getLinkage(),
      // If the original global variable had an initializer, replace it with the
      // null pointer initializer
      !OrigGV->isDeclaration() ? Constant::getNullValue(NewGVTy) : nullptr,
      OrigGV->getName() + "_heapify", /* InsertBefore */ nullptr,
      OrigGV->getThreadLocalMode(), OrigGV->getType()->getAddressSpace(),
      OrigGV->isExternallyInitialized());
  NewGV->copyAttributesFrom(OrigGV);
  NewGV->setAlignment(0);

  // Copy debug info
  SmallVector<DIGlobalVariableExpression *, 1> GVs;
  OrigGV->getDebugInfo(GVs);
  for (auto *GV : GVs) {
    NewGV->addDebugInfo(GV);
  }

  if (!OrigGV->isDeclaration()) {
    initializeHeapifiedGlobalVariable(OrigGV, NewGV);
    NumOfGlobalVariableArrayHeapification++;
  }

  // Now that the global variable has been heapified to the heap, it must be
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

  for (auto *U : CEUsers) {
    expandConstantExpression(U);
    U->destroyConstant();
  }

  // Update all the users of the original global variable (including the
  // newly-expanded constant expressions) to use the dynamically allocated array
  SmallVector<User *, 8> Users(OrigGV->user_begin(), OrigGV->user_end());

  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewGV);
    } else if (auto *PHI = dyn_cast<PHINode>(U)) {
      // PHI nodes are a special case because they must always be the first
      // instruction in a basic block. To ensure this property is true we insert
      // the load instruction at the end of the appropriate predecessor block(s)
      for (unsigned I = 0; I < PHI->getNumIncomingValues(); ++I) {
        Value *IncomingValue = PHI->getIncomingValue(I);
        BasicBlock *IncomingBlock = PHI->getIncomingBlock(I);

        if (IncomingValue == OrigGV) {
          auto *LoadNewGV =
              new LoadInst(NewGV, "", IncomingBlock->getTerminator());
          auto *BitCastNewGV =
              CastInst::CreatePointerCast(LoadNewGV, IncomingValue->getType(),
                                          "", IncomingBlock->getTerminator());
          PHI->setIncomingValue(I, BitCastNewGV);
        }
      }
    } else if (auto *Select = dyn_cast<SelectInst>(U)) {
      // Ensure selects are correctly typed
      updateSelect(Select, OrigGV, NewGV);
    } else if (auto *Return = dyn_cast<ReturnInst>(U)) {
      // Ensure returns are correctly typed to the funtion type
      updateReturn(Return, OrigGV, NewGV);
    } else if (auto *Inst = dyn_cast<Instruction>(U)) {
      // We must load the array from the heap before we can do anything with it
      auto *LoadNewGV = new LoadInst(NewGV, "", Inst);
      Inst->replaceUsesOfWith(OrigGV, LoadNewGV);
    } else {
      assert(false && "Unsupported global variable user");
    }
  }

  if (!NewGV->isDeclaration()) {
    IRBuilder<> IRB = createHeapifyDtor(NewGV);

    insertFree(NewGV, &*IRB.GetInsertPoint());
    NumOfFreeInsert++;
  }

  OrigGV->eraseFromParent();

  return NewGV;
}

bool HeapifyGlobalVariables::runOnModule(Module &M) {
  const DataLayout &DL = M.getDataLayout();

  // Global variables to heapify
  SmallPtrSet<GlobalVariable *, 8> GVsToHeapify;

  // Heapified global variables
  SmallPtrSet<Value *, 8> HeapifiedGVs;

  for (auto &GV : M.globals()) {
    // Skip LLVM intrinsics
    if (GV.getName().startswith("llvm.")) {
      continue;
    }

    // Skip C++ junk
    if (isVTableOrTypeInfo(&GV)) {
      continue;
    }

    if (GV.isConstant() &&
        (GV.hasPrivateLinkage() || GV.hasInternalLinkage())) {
      continue;
    }

    if (isHeapifiableType(GV.getValueType())) {
      GVsToHeapify.insert(&GV);
    }
  }

  // Heapify non-constant global static arrays in a module constructor and free
  // them in a destructor
  if (!GVsToHeapify.empty()) {
    for (auto *GV : GVsToHeapify) {
      auto *HeapifiedGV = heapifyGlobalVariable(GV);
      HeapifiedGVs.insert(HeapifiedGV);
    }
  }

  // Loads and stores to the newly-heapified global variables may not be aligned
  // correctly for memory on the heap. To be safe we set the alignment to 1,
  // which is "always safe" (according to the LLVM docs)
  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Load = dyn_cast<LoadInst>(&*I)) {
        auto *Obj =
            GetUnderlyingObjectThroughLoads(Load->getPointerOperand(), DL);
        if (HeapifiedGVs.count(Obj) > 0) {
          Load->setAlignment(1);
        }
      } else if (auto *Store = dyn_cast<StoreInst>(&*I)) {
        auto *Obj =
            GetUnderlyingObjectThroughLoads(Store->getPointerOperand(), DL);
        if (HeapifiedGVs.count(Obj) > 0) {
          Store->setAlignment(1);
        }
      } else if (auto *MemI = dyn_cast<MemIntrinsic>(&*I)) {
        auto *Obj = GetUnderlyingObjectThroughLoads(MemI->getDest(), DL);
        if (HeapifiedGVs.count(Obj) > 0) {
          MemI->setDestAlignment(1);
        }
      }
    }
  }

  printStatistic(M, NumOfGlobalVariableArrayHeapification);
  printStatistic(M, NumOfFreeInsert);

  return NumOfGlobalVariableArrayHeapification > 0;
}

static RegisterPass<HeapifyGlobalVariables>
    X("fuzzalloc-heapify-global-vars",
      "Heapify static global variable arrays to malloc calls", false, false);

static void registerHeapifyGlobalVariablesPass(const PassManagerBuilder &,
                                               legacy::PassManagerBase &PM) {
  PM.add(new HeapifyGlobalVariables());
}

static RegisterStandardPasses RegisterHeapifyGlobalVariablesPass(
    PassManagerBuilder::EP_ModuleOptimizerEarly,
    registerHeapifyGlobalVariablesPass);

static RegisterStandardPasses RegisterHeapifyGlobalVariablesPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0,
    registerHeapifyGlobalVariablesPass);
