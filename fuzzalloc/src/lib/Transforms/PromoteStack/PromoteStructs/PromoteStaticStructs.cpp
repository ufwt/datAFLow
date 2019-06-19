//===-- PromoteStaticStructs.cpp - Promote static arrays to mallocs
//--------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass promotes structs (both global and stack-based) containing static
/// arrays to dynamically allocated structs via \p malloc.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "PromoteCommon.h"
#include "debug.h" // from afl

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-prom-static-structs"

static cl::opt<unsigned>
    ClMinArraySize("fuzzalloc-min-array-size",
                   cl::desc("The minimum size of a static array inside a "
                            "struct to promote to malloc"),
                   cl::init(1));

STATISTIC(NumOfAllocaStructPromotion, "Number of alloca struct promotions.");
STATISTIC(NumOfGlobalVariableStructPromotion,
          "Number of global variable struct promotions.");
STATISTIC(NumOfFreeInsert, "Number of calls to free inserted.");

namespace {

/// PromoteStaticStructs: instrument the code in a module to promote structs
/// (both global and stack-based) containing static arrays to dynamically
/// allocated structs via \p malloc.
class PromoteStaticStructs : public ModulePass {
private:
  IntegerType *IntPtrTy;

  Instruction *createStructMalloc(IRBuilder<> &, StructType *);
  AllocaInst *promoteStructAlloca(AllocaInst *);

public:
  static char ID;
  PromoteStaticStructs() : ModulePass(ID) {}

  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

char PromoteStaticStructs::ID = 0;

/// Returns \p true if the struct contains a nested array, or \p false
/// othewise.
///
/// Nested structs are also checked.
static bool structContainsArray(const StructType *StructTy) {
  for (auto *Elem : StructTy->elements()) {
    if (auto *ArrayElem = dyn_cast<ArrayType>(Elem)) {
      if (ArrayElem->getNumElements() >= ClMinArraySize) {
        return true;
      }
    } else if (auto *StructElem = dyn_cast<StructType>(Elem)) {
      return structContainsArray(StructElem);
    }
  }

  return false;
}

static Value *updateGEP(GetElementPtrInst *GEP, Instruction *MallocPtr) {
  IRBuilder<> IRB(GEP);

  // Load the pointer to the dynamically allocated array and create a new GEP
  // instruction
  auto *Load = IRB.CreateLoad(MallocPtr);
  auto *NewGEP = IRB.CreateInBoundsGEP(
      Load, SmallVector<Value *, 4>(GEP->idx_begin(), GEP->idx_end()),
      GEP->hasName() ? GEP->getName() + "_prom" : "");

  // Update all the users of the original GEP instruction to use the updated
  // GEP. The updated GEP is correctly typed for the malloc pointer
  GEP->replaceAllUsesWith(NewGEP);

  return NewGEP;
}

Instruction *PromoteStaticStructs::createStructMalloc(IRBuilder<> &IRB,
                                                      StructType *AllocTy) {
  Constant *SizeOfStruct = ConstantExpr::getSizeOf(AllocTy);

  return CallInst::CreateMalloc(&*IRB.GetInsertPoint(), this->IntPtrTy, AllocTy,
                                SizeOfStruct,
                                ConstantInt::get(this->IntPtrTy, 1), nullptr);
}

AllocaInst *PromoteStaticStructs::promoteStructAlloca(AllocaInst *Alloca) {
  LLVM_DEBUG(dbgs() << "promoting" << *Alloca << '\n');

  // Cache uses
  SmallVector<User *, 8> Users(Alloca->user_begin(), Alloca->user_end());

  const Module *M = Alloca->getModule();
  LLVMContext &C = M->getContext();

  // This is safe because we already know that the alloca has a struct type
  StructType *StructTy = cast<StructType>(Alloca->getAllocatedType());

  IRBuilder<> IRB(Alloca);

  // This will transform something like this:
  //
  // %1 = alloca StructTy
  //
  // where `StructTy` contains a static array, into something like this:
  //
  // %1 = alloca StructTy*
  // %2 = call i8* @malloc(StructTy)
  // %2 = bitcast i8* %2 to StructTy*
  // store StructTy* %3, StructTy** %1
  //
  // Where:
  //
  //  - `StructTy` is a struct type containing a static array
  auto *NewAlloca = IRB.CreateAlloca(StructTy->getPointerTo(), nullptr,
                                     Alloca->getName() + "_prom");
  auto *MallocCall = createStructMalloc(IRB, StructTy);
  auto *MallocStore = IRB.CreateStore(MallocCall, NewAlloca);
  MallocStore->setMetadata(M->getMDKindID("fuzzalloc.noinstrument"),
                           MDNode::get(C, None));

  // Update all the users of the original struct to use the dynamically
  // allocated structs
  for (auto *U : Users) {
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Ensure GEPs are correctly typed
      updateGEP(GEP, NewAlloca);
      GEP->eraseFromParent();
    } else if (auto *Call = dyn_cast<CallInst>(U)) {
      // Dereference escaped structs
      // TODO escape analysis
      auto *MallocLoad =
          new LoadInst(NewAlloca, NewAlloca->getName() + "_deref", Call);
      U->replaceUsesOfWith(Alloca, MallocLoad);
    } else {
      U->replaceUsesOfWith(Alloca, NewAlloca);
    }
  }

  return NewAlloca;
}

bool PromoteStaticStructs::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();

  this->IntPtrTy = M.getDataLayout().getIntPtrType(C);

  return false;
}

bool PromoteStaticStructs::runOnModule(Module &M) {
  // Retrieve all of the structs defined in this module
  TypeFinder StructTypes;
  StructTypes.run(M, /* OnlyNamed */ false);

  // Struct types containing static arrays that need to be promoted
  SmallPtrSet<StructType *, 8> StructsToPromote;

  for (auto *Ty : StructTypes) {
    if (structContainsArray(Ty)) {
      StructsToPromote.insert(Ty);
    }
  }

  // Struct allocations that contain a static array to promote
  SmallVector<AllocaInst *, 8> StructAllocasToPromote;

  for (auto &F : M.functions()) {
    StructAllocasToPromote.clear();

    // lifetime.end intrinsics that may require calls to free to be inserted
    // before them
    SmallVector<IntrinsicInst *, 4> LifetimeEnds;

    // List of return instructions that may require calls to free to be
    // inserted before them
    SmallVector<ReturnInst *, 4> Returns;

    // Collect allocas to promote
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
        if (auto *StructTy = dyn_cast<StructType>(Alloca->getAllocatedType())) {
          if (StructsToPromote.find(StructTy) != StructsToPromote.end()) {
            StructAllocasToPromote.push_back(Alloca);
          }
        }
      } else if (auto *Intrinsic = dyn_cast<IntrinsicInst>(&*I)) {
        if (Intrinsic->getIntrinsicID() == Intrinsic::lifetime_end) {
          LifetimeEnds.push_back(Intrinsic);
        }
      } else if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
        Returns.push_back(Return);
      }
    }

    // Promote structs containing static arrays to dynamically allocated
    // structs and insert calls to free at the appropriate locations (either at
    // lifetime.end intrinsics or at return instructions)
    SmallVector<IntrinsicInst *, 8> LifetimeEndsToInsertFree;

    for (auto *Alloca : StructAllocasToPromote) {
      LifetimeEndsToInsertFree.clear();

      // Check if any of the original allocas are used in any of the
      // lifetime.end intrinsics. If they are, we should insert the free before
      // the lifetime.end intrinsic and NOT at every return, otherwise we may
      // end up with a double free :(

      // Promote the alloca
      auto *NewAlloca = promoteStructAlloca(Alloca);

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
      NumOfAllocaStructPromotion++;
    }
  }

  // TODO promote global structs
  (void)NumOfGlobalVariableStructPromotion;

  if (NumOfAllocaStructPromotion > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfAllocaStructPromotion.getValue(),
        NumOfAllocaStructPromotion.getName(),
        NumOfAllocaStructPromotion.getDesc());
  }

  return (NumOfAllocaStructPromotion > 0) ||
         (NumOfGlobalVariableStructPromotion > 0);
}

static RegisterPass<PromoteStaticStructs>
    X("fuzzalloc-prom-static-structs",
      "Promote static structs containing static arrays to malloc calls", false,
      false);

static void registerPromoteStaticStructsPass(const PassManagerBuilder &,
                                             legacy::PassManagerBase &PM) {
  PM.add(new PromoteStaticStructs());
}

static RegisterStandardPasses
    RegisterPromoteStaticStructsPass(PassManagerBuilder::EP_OptimizerLast,
                                     registerPromoteStaticStructsPass);

static RegisterStandardPasses
    RegisterPromoteStaticStructsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                      registerPromoteStaticStructsPass);
