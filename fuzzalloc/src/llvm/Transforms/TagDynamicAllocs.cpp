//===-- TagDynamicAllocs.cpp - Tag dynamic memory allocs with a unique ID -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass tags calls to dynamic memory allocation functions (e.g.,
/// \p malloc, \p calloc, etc.) with a randomly-generated identifier. The
/// original function calls are redirected to fuzzalloc's wrapper functions.
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
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "tag-dyn-alloc"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

STATISTIC(NumOfTaggedMalloc, "Number of malloc calls tagged.");
STATISTIC(NumOfTaggedCalloc, "Number of calloc calls tagged.");
STATISTIC(NumOfTaggedRealloc, "Number of realloc calls tagged.");

namespace {

static const char *const TaggedMallocName = "__tagged_malloc";
static const char *const TaggedCallocName = "__tagged_calloc";
static const char *const TaggedReallocName = "__tagged_realloc";

/// TagMalloc: tag \p malloc, \p calloc and \p realloc calls with a
/// randomly-generated identifier and call fuzzalloc's \p malloc (or \p calloc,
/// or \p realloc) wrapper function with this tag.
class TagMalloc : public ModulePass {
public:
  static char ID;
  TagMalloc() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
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

static bool isReallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                            bool LookThroughBitCast = false) {
  return isAllocationFn(V, TLI, LookThroughBitCast) &&
         !isAllocLikeFn(V, TLI, LookThroughBitCast);
}

static CallInst *extractReallocCall(Value *I, const TargetLibraryInfo *TLI) {
  return isReallocLikeFn(I, TLI) ? cast<CallInst>(I) : nullptr;
}

char TagMalloc::ID = 0;

bool TagMalloc::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);
  IntegerType *TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  IntegerType *SizeTTy = DL.getIntPtrType(C);

  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI(TLII);

  // The malloc/calloc/realloc wrapper functions take the same arguments as the
  // original function, except that the first argument is tag that represents
  // the allocation site
  Function *MallocWrapperF = checkAllocWrapperFunction(
      M.getOrInsertFunction(TaggedMallocName, Int8PtrTy, TagTy, SizeTTy));
  Function *CallocWrapperF = checkAllocWrapperFunction(M.getOrInsertFunction(
      TaggedCallocName, Int8PtrTy, TagTy, SizeTTy, SizeTTy));
  Function *ReallocWrapperF = checkAllocWrapperFunction(M.getOrInsertFunction(
      TaggedReallocName, Int8PtrTy, TagTy, Int8PtrTy, SizeTTy));

  // Maps malloc/calloc/realloc calls to the appropriate malloc/calloc/realloc
  // wrapper function
  std::map<CallInst *, Function *> AllocCalls;

  for (auto &F : M.functions()) {
    for (auto I = inst_begin(F); I != inst_end(F); ++I) {
      if (auto *MallocCall = extractMallocCall(&*I, &TLI)) {
        AllocCalls.emplace(MallocCall, MallocWrapperF);
        NumOfTaggedMalloc++;
      } else if (auto *CallocCall = extractCallocCall(&*I, &TLI)) {
        AllocCalls.emplace(CallocCall, CallocWrapperF);
        NumOfTaggedCalloc++;
      } else if (auto *ReallocCall = extractReallocCall(&*I, &TLI)) {
        AllocCalls.emplace(ReallocCall, ReallocWrapperF);
        NumOfTaggedRealloc++;
      }
    }
  }

  for (auto &AllocCallWithWrapper : AllocCalls) {
    CallInst *AllocCall = AllocCallWithWrapper.first;
    Function *WrapperF = AllocCallWithWrapper.second;

    // Generate a random tag representing the allocation site
    ConstantInt *Tag = ConstantInt::get(TagTy, RAND(DEFAULT_TAG + 1, TAG_MAX));
    SmallVector<Value *, 3> WrapperArgs = {Tag};

    // Copy the original allocation call's arguments after the tag
    for (auto &Arg : AllocCall->arg_operands()) {
      WrapperArgs.push_back(&*Arg);
    }

    CallInst *AllocWrapperCall =
        CallInst::Create(WrapperF, WrapperArgs, "", AllocCall);

    // Replace all uses of the original function return value with the return
    // value from the wrapped function
    for (auto *U : AllocCall->users()) {
      U->replaceUsesOfWith(AllocCall, AllocWrapperCall);
    }

    AllocCall->eraseFromParent();
  }

  return true;
}

static RegisterPass<TagMalloc> X("tag-dyn-alloc",
                                 "Tag dynamic allocation function calls and "
                                 "replace them with a call to fuzzalloc's "
                                 "wrapper functions",
                                 false, false);
