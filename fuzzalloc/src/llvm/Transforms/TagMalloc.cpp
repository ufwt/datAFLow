//===-- TagMalloc.cpp - Tag mallocs with a unique identifier --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass tags calls to \p malloc with a randomly-generated identifier
/// and calls fuzzalloc's \p malloc wrapper function.
///
//===----------------------------------------------------------------------===//

#include <map>
#include <stdint.h>
#include <stdlib.h>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "tag-malloc"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) (x + random() / (RAND_MAX / (y - x + 1) + 1))

STATISTIC(NumOfTaggedMalloc, "Number of malloc calls tagged.");
STATISTIC(NumOfTaggedCalloc, "Number of calloc calls tagged.");

namespace {

static const char *const TaggedMallocName = "__tagged_malloc";
static const char *const TaggedCallocName = "__tagged_calloc";

/// TagMalloc: tag \p malloc and \p calloc calls with a randomly-generated
/// identifier and call fuzzalloc's \p malloc (or \p calloc) wrapper function
/// with this tag.
class TagMalloc : public ModulePass {
public:
  static char ID;
  TagMalloc() : ModulePass(ID) {}

  bool runOnModule(Module &F) override;
};

} // end anonymous namespace

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkAllocWrapperFunction(Constant *FuncOrBitcast) {
  if (isa<Function>(FuncOrBitcast)) {
    return cast<Function>(FuncOrBitcast);
  }

  FuncOrBitcast->print(errs());
  errs() << '\n';
  std::string Err;
  raw_string_ostream Stream(Err);
  Stream << "Allocation wrapper function redefined: " << *FuncOrBitcast;
  report_fatal_error(Err);
}

char TagMalloc::ID = 0;

bool TagMalloc::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);
  IntegerType *Int16Ty = Type::getInt16Ty(C);
  IntegerType *IntPtrTy = DL.getIntPtrType(C);

  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);

  // The malloc/calloc wrapper functions take the same arguments as the
  // original function, except that the first argument is an unsigned 16-bit
  // tag for the allocation site
  Function *MallocWrapperF = checkAllocWrapperFunction(
      M.getOrInsertFunction(TaggedMallocName, Int8PtrTy, Int16Ty, IntPtrTy));
  Function *CallocWrapperF = checkAllocWrapperFunction(M.getOrInsertFunction(
      TaggedCallocName, Int8PtrTy, Int16Ty, IntPtrTy, IntPtrTy));

  // Maps malloc/calloc calls to the appropriate malloc/calloc wrapper function
  std::map<CallInst *, Function *> AllocCalls;

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *MallocCall = extractMallocCall(&*I, &TLI)) {
        AllocCalls.emplace(MallocCall, MallocWrapperF);
        NumOfTaggedMalloc++;
      } else if (auto *CallocCall = extractCallocCall(&*I, &TLI)) {
        AllocCalls.emplace(CallocCall, CallocWrapperF);
        NumOfTaggedCalloc++;
      }
    }
  }

  for (auto &AllocCallWithWrapper : AllocCalls) {
    CallInst *AllocCall = AllocCallWithWrapper.first;
    Function *WrapperF = AllocCallWithWrapper.second;

    // Generate a random 16-bit tag representing the allocation site
    ConstantInt *Tag = ConstantInt::get(Int16Ty, (uint16_t)RAND(1, UINT16_MAX));
    SmallVector<Value *, 3> WrapperArgs = {Tag};

    // Copy the original malloc/calloc call's arguments after the tag
    for (auto &Arg : AllocCall->arg_operands()) {
      WrapperArgs.push_back(&*Arg);
    }

    CallInst *AllocWrapperCall =
        CallInst::Create(WrapperF, WrapperArgs, "", AllocCall);

    // Replace all uses of the original malloc/calloc result with the wrapped
    // result
    for (auto *U : AllocCall->users()) {
      U->replaceUsesOfWith(AllocCall, AllocWrapperCall);
    }

    AllocCall->eraseFromParent();
  }

  return true;
}

static RegisterPass<TagMalloc> X("tag-malloc",
                                 "Tag malloc calls and replace them with a "
                                 "call to fuzzalloc's malloc wrapper",
                                 false, false);

static void registerTagMallocPass(const PassManagerBuilder &,
                                  legacy::PassManagerBase &PM) {
  PM.add(new TagMalloc());
}

static RegisterStandardPasses
    RegisterTagMallocPass(PassManagerBuilder::EP_OptimizerLast,
                          registerTagMallocPass);

static RegisterStandardPasses
    RegisterTagMallocPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                           registerTagMallocPass);
