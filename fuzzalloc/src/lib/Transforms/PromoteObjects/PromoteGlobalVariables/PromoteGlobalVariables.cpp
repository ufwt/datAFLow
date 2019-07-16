//===-- PromoteGlobalVariables.cpp - Promote global var. arrays to mallocs ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass promotes static global variable arrays to dynamically-allocated
/// arrays via \p malloc.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
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
#include "PromoteCommon.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-prom-global-vars"

static cl::opt<int>
    ClMinArraySize("fuzzalloc-min-global-array-size",
                   cl::desc("The minimum size of a static global variable "
                            "array to promote to malloc"),
                   cl::init(1));

STATISTIC(NumOfGlobalVariableArrayPromotion,
          "Number of global variable array promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteGlobalVariables: instrument the code in a module to promote static,
/// fixed-size global variable arrays to dynamically-allocated arrays via
/// \p malloc.
class PromoteGlobalVariables : public ModulePass {
private:
  GlobalVariable *promoteGlobalVariable(GlobalVariable *, Function *) const;

public:
  static char ID;
  PromoteGlobalVariables() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteGlobalVariables::ID = 0;

/// Create a constructor function that will be used to `malloc` all of the
/// promoted global variables in the module.
static Function *createArrayPromCtor(Module &M) {
  LLVMContext &C = M.getContext();

  FunctionType *GlobalCtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__init_prom_global_arrays_" + M.getName(), &M);
  appendToGlobalCtors(M, GlobalCtorF, 0);

  BasicBlock *GlobalCtorBB = BasicBlock::Create(C, "", GlobalCtorF);
  ReturnInst::Create(C, GlobalCtorBB);

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
  appendToGlobalDtors(M, GlobalDtorF, 0);

  BasicBlock *GlobalDtorBB = BasicBlock::Create(C, "", GlobalDtorF);
  ReturnInst::Create(C, GlobalDtorBB);

  return GlobalDtorF;
}

/// Returns `true` if the given global variable uses one of the global variables
/// that is going to be promoted in its initializer.
static bool
usesPromotableGlobalVariable(const GlobalVariable *GV,
                             const SetVector<GlobalVariable *> &GVsToPromote) {
  if (!GV->hasInitializer()) {
    return false;
  }

  // TODO only support ConstantAggregate initializers for now
  const Constant *Initializer = GV->getInitializer();
  if (!isa<ConstantAggregate>(Initializer)) {
    return false;
  }

  SmallVector<Value *, 8> Worklist(Initializer->operands());

  while (!Worklist.empty()) {
    // Global variable initializers can only use constants
    Constant *C = cast<Constant>(Worklist.pop_back_val());

    if (auto *GVC = dyn_cast<GlobalVariable>(C)) {
      if (GVsToPromote.count(GVC) > 0) {
        return true;
      }
    }

    Worklist.append(C->op_begin(), C->op_end());
  }

  return false;
}

static void expandConstantExpression(ConstantExpr *ConstExpr) {
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
      // PHI nodes are handled differently because they must always be the first
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
    } else {
      assert(false && "Unsupported constant expression user");
    }
  }

  ConstExpr->destroyConstant();
}

/// Recursively expand `ConstantAggregate`s by generating equivalent
/// instructions.
static void expandConstantAggregate(IRBuilder<> &IRB, GlobalVariable *GV,
                                    ConstantAggregate *ConstantAgg,
                                    std::vector<unsigned> &Idxs) {
  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();
  IntegerType *Int32Ty = Type::getInt32Ty(C);
  auto UnsignedToInt32 = [Int32Ty](const unsigned &N) {
    return ConstantInt::get(Int32Ty, N);
  };

  // TODO handle ConstantAggregates with multiple users
  assert(ConstantAgg->hasOneUse());

  for (unsigned I = 0; I < ConstantAgg->getNumOperands(); ++I) {
    auto *Op = ConstantAgg->getOperand(I);

    if (auto *AggregateOp = dyn_cast<ConstantAggregate>(Op)) {
      // Expand the nested ConstantAggregate
      Idxs.push_back(I);
      expandConstantAggregate(IRB, GV, AggregateOp, Idxs);
      Idxs.pop_back();
    } else {
      std::vector<Value *> IdxValues(Idxs.size());
      std::transform(Idxs.begin(), Idxs.end(), IdxValues.begin(),
                     UnsignedToInt32);
      IdxValues.push_back(UnsignedToInt32(I));

      auto *Store = IRB.CreateStore(Op, IRB.CreateInBoundsGEP(GV, IdxValues));
      Store->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                         MDNode::get(C, None));
    }
  }

  // XXX I don't understand why the num. uses doesn't automatically go to zero
  ConstantAgg->dropAllReferences();
}

/// Move global variable's who have a `ConstantAggregate` initializer into a
/// constructor function.
static Function *expandGlobalVariableInitializer(GlobalVariable *GV) {
  LLVM_DEBUG(dbgs() << "rewriting initializer for global variable " << *GV
                    << '\n');

  Module *M = GV->getParent();
  LLVMContext &C = M->getContext();
  Constant *Initializer = GV->getInitializer();

  assert(isa<ConstantAggregate>(Initializer));

  FunctionType *GlobalCtorTy = FunctionType::get(Type::getVoidTy(C), false);
  Function *GlobalCtorF =
      Function::Create(GlobalCtorTy, GlobalValue::LinkageTypes::InternalLinkage,
                       "__init_non_constant_globals_" + M->getName(), M);
  appendToGlobalCtors(*M, GlobalCtorF, 1);

  BasicBlock *GlobalCtorBB = BasicBlock::Create(C, "", GlobalCtorF);

  IRBuilder<> IRB(GlobalCtorBB);
  for (unsigned I = 0; I < Initializer->getNumOperands(); ++I) {
    auto *Op = Initializer->getOperand(I);

    if (auto *AggregateOp = dyn_cast<ConstantAggregate>(Op)) {
      std::vector<unsigned> Idxs = {0, I};
      expandConstantAggregate(IRB, GV, AggregateOp, Idxs);
    } else {
      auto *Store = IRB.CreateStore(
          Op, IRB.CreateConstInBoundsGEP2_32(nullptr, GV, 0, I));
      Store->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                         MDNode::get(C, None));
    }
  }
  IRB.CreateRetVoid();

  GV->setInitializer(ConstantAggregateZero::get(GV->getValueType()));

  assert(!Initializer->isConstantUsed() &&
         "Initializer should no longer be used");
  Initializer->destroyConstant();

  return GlobalCtorF;
}

GlobalVariable *
PromoteGlobalVariables::promoteGlobalVariable(GlobalVariable *OrigGV,
                                              Function *ArrayPromCtor) const {
  LLVM_DEBUG(dbgs() << "promoting " << *OrigGV << '\n');

  Module *M = OrigGV->getParent();
  LLVMContext &C = M->getContext();
  const DataLayout &DL = M->getDataLayout();

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

  auto *MallocCall = createArrayMalloc(C, DL, IRB, ElemTy, ArrayNumElems);

  // If the array had an initializer, we must replicate it so that the malloc'd
  // memory contains the same data when it is first used. How we do this depends
  // on the initializer
  if (OrigGV->hasInitializer()) {
    if (isa<ConstantAggregateZero>(OrigGV->getInitializer())) {
      // If the initializer is the zeroinitializer, just memset the dynamically
      // allocated memory to zero. Likewise with promoted allocas that are
      // memset, reset the destination alignment
      uint64_t Size = DL.getTypeAllocSize(ElemTy) * ArrayNumElems;
      auto MemSetCall =
          IRB.CreateMemSet(MallocCall, Constant::getNullValue(IRB.getInt8Ty()),
                           Size, OrigGV->getAlignment());
      cast<MemIntrinsic>(MemSetCall)->setDestAlignment(0);
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
    } else if (auto *Initializer =
                   dyn_cast<ConstantArray>(OrigGV->getInitializer())) {
      // Similarly for constant arrays
      for (unsigned I = 0; I < Initializer->getNumOperands(); ++I) {
        auto *StoreToNewGV = IRB.CreateStore(
            Initializer->getOperand(I),
            IRB.CreateConstInBoundsGEP1_32(nullptr, MallocCall, I));
        StoreToNewGV->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                                  MDNode::get(C, None));
      }
    } else {
      assert(false && "Unsupported global variable initializer");
    }
  }

  auto *MallocStore = IRB.CreateStore(MallocCall, NewGV);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.no_instrument"),
                           MDNode::get(C, None));

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

  // Update all the users of the original global variable (including the
  // newly-expanded constant expressions) to use the dynamically allocated array
  SmallVector<User *, 8> Users(OrigGV->user_begin(), OrigGV->user_end());

  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewGV);
      GEP->eraseFromParent();
    } else if (auto *Inst = dyn_cast<Instruction>(U)) {
      auto *LoadNewGV = new LoadInst(NewGV, "", Inst);
      U->replaceUsesOfWith(OrigGV, LoadNewGV);
    } else {
      assert(false && "Unsupported user");
    }
  }

  return NewGV;
}

bool PromoteGlobalVariables::runOnModule(Module &M) {
  // Collect global variables to promote
  SetVector<GlobalVariable *> GVsToPromote;

  for (auto &GV : M.globals()) {
    StringRef GVName = GV.getName();
    if (GVName == "llvm.global_ctors" || GVName == "llvm.global_dtors") {
      continue;
    }

    if (isPromotableType(GV.getValueType()) && !GV.isConstant()) {
      GVsToPromote.insert(&GV);
    }
  }

  // Rewrite global variables that use promotable global variables in their
  // initializer (otherwise this would break expandConstantExpressions because
  // the initializers are not instructions and therefore cannot be rewritten as
  // such)
  for (auto &GV : M.globals()) {
    if (usesPromotableGlobalVariable(&GV, GVsToPromote)) {
      expandGlobalVariableInitializer(&GV);
    }
  }

  // Promote non-constant global static arrays in a module constructor and free
  // them in a destructor
  if (!GVsToPromote.empty()) {
    Function *GlobalCtorF = createArrayPromCtor(M);
    Function *GlobalDtorF = createArrayPromDtor(M);

    for (auto *GV : GVsToPromote) {
      auto *PromotedGV = promoteGlobalVariable(GV, GlobalCtorF);
      NumOfGlobalVariableArrayPromotion++;

      if (!PromotedGV->isDeclaration()) {
        insertFree(PromotedGV, GlobalDtorF->getEntryBlock().getTerminator());
        NumOfFreeInsert++;
      }

      GV->eraseFromParent();
    }
  }

  printStatistic(M, NumOfGlobalVariableArrayPromotion);

  return NumOfGlobalVariableArrayPromotion > 0;
}

static RegisterPass<PromoteGlobalVariables>
    X("fuzzalloc-prom-global-vars",
      "Promote static global variable arrays to malloc calls", false, false);

static void registerPromoteGlobalVariablesPass(const PassManagerBuilder &,
                                               legacy::PassManagerBase &PM) {
  PM.add(new PromoteGlobalVariables());
}

static RegisterStandardPasses
    RegisterPromoteGlobalVariablesPass(PassManagerBuilder::EP_OptimizerLast,
                                       registerPromoteGlobalVariablesPass);

static RegisterStandardPasses RegisterPromoteGlobalVariablesPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0,
    registerPromoteGlobalVariablesPass);
