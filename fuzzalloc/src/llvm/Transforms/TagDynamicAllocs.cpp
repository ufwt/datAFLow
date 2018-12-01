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
/// original function calls are redirected to fuzzalloc's tagged wrapper
/// functions.
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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "tag-dyn-alloc"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

static cl::OptionCategory
    WrapperFuncCat("Memory allocation wrapper functions",
                   "Sometimes malloc/calloc/realloc aren't called directly, "
                   "but via a custom wrapper function. This lets us instrument "
                   "the wrapper function instead");

static cl::opt<std::string> ClMallocWrapperName(
    "malloc-wrapper",
    cl::desc("Custom malloc wrapper function to instrument instead of malloc"),
    cl::cat(WrapperFuncCat));

static cl::opt<std::string> ClCallocWrapperName(
    "calloc-wrapper",
    cl::desc("Custom calloc wrapper function to instrument instead of calloc"),
    cl::cat(WrapperFuncCat));

static cl::opt<std::string> ClReallocWrapperName(
    "realloc-wrapper",
    cl::desc(
        "Custom realloc wrapper function to instrument instead of realloc"),
    cl::cat(WrapperFuncCat));

STATISTIC(NumOfTaggedMalloc, "Number of malloc calls tagged.");
STATISTIC(NumOfTaggedCalloc, "Number of calloc calls tagged.");
STATISTIC(NumOfTaggedRealloc, "Number of realloc calls tagged.");

namespace {

static const char *const TaggedMallocName = "__tagged_malloc";
static const char *const TaggedCallocName = "__tagged_calloc";
static const char *const TaggedReallocName = "__tagged_realloc";

/// TagDynamicAlloc: Tag \p malloc, \p calloc and \p realloc calls with a
/// randomly-generated identifier (to identify their call site) and call
/// fuzzalloc's tagged \p malloc (or \p calloc, or \p realloc) instead.
class TagDynamicAlloc : public ModulePass {
private:
  Function *FuzzallocMallocF;
  Function *FuzzallocCallocF;
  Function *FuzzallocReallocF;

  Function *TaggedCustomMallocF;
  Function *TaggedCustomCallocF;
  Function *TaggedCustomReallocF;

  IntegerType *TagTy;
  IntegerType *SizeTTy;

  std::map<CallInst *, Function *>
  getDynAllocCalls(Function *F, const TargetLibraryInfo *TLI);
  CallInst *tagDynAllocCall(CallInst *AllocCall, Function *WrapperF,
                            Value *Tag);
  Function *tagAllocWrapperFunc(Function *F, const TargetLibraryInfo *TLI);

public:
  static char ID;
  TagDynamicAlloc() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkFuzzallocFunction(Constant *FuncOrBitcast) {
  if (isa<Function>(FuncOrBitcast)) {
    return cast<Function>(FuncOrBitcast);
  }

  FuncOrBitcast->print(errs());
  errs() << '\n';
  std::string Err;
  raw_string_ostream Stream(Err);
  Stream << "fuzzalloc function redefined: " << *FuncOrBitcast;
  report_fatal_error(Err);
}

static bool isReallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                            bool LookThroughBitCast = false) {
  return isAllocationFn(V, TLI, LookThroughBitCast) &&
         !isAllocLikeFn(V, TLI, LookThroughBitCast);
}

char TagDynamicAlloc::ID = 0;

/// Maps \p malloc / \p calloc / \p realloc calls in a given function to the
/// corresponding tagged \p malloc / \p calloc / \p realloc wrapper function
/// that the original function call should be replaced with.
std::map<CallInst *, Function *>
TagDynamicAlloc::getDynAllocCalls(Function *F, const TargetLibraryInfo *TLI) {
  // Maps calls to malloc/calloc/realloc to the corresponding fuzzalloc
  // function
  std::map<CallInst *, Function *> AllocCalls;

  for (auto I = inst_begin(F); I != inst_end(F); ++I) {
    if (auto *Call = dyn_cast<CallInst>(&*I)) {
      auto *CalledFunc = Call->getCalledFunction();

      if (!ClMallocWrapperName.empty() && CalledFunc &&
          CalledFunc->getName() == ClMallocWrapperName) {
        AllocCalls.emplace(Call, this->TaggedCustomMallocF);
        NumOfTaggedMalloc++;
      } else if (isMallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocMallocF);
        NumOfTaggedMalloc++;
      } else if (!ClCallocWrapperName.empty() && CalledFunc &&
                 CalledFunc->getName() == ClCallocWrapperName) {
        AllocCalls.emplace(Call, this->TaggedCustomCallocF);
        NumOfTaggedCalloc++;
      } else if (isCallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocCallocF);
        NumOfTaggedCalloc++;
      } else if (!ClReallocWrapperName.empty() && CalledFunc &&
                 CalledFunc->getName() == ClReallocWrapperName) {
        AllocCalls.emplace(Call, this->TaggedCustomReallocF);
        NumOfTaggedRealloc++;
      } else if (isReallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocReallocF);
        NumOfTaggedRealloc++;
      }
    }
  }

  return AllocCalls;
}

/// Tag \p malloc / \p calloc / \p realloc calls with a call site identifier
/// and call the corresponding fuzzalloc function instead.
CallInst *TagDynamicAlloc::tagDynAllocCall(CallInst *AllocCall,
                                           Function *WrapperF, Value *Tag) {
  // Copy the original allocation function call's arguments after the tag
  SmallVector<Value *, 3> WrapperArgs = {Tag};
  WrapperArgs.insert(WrapperArgs.end(), AllocCall->arg_begin(),
                     AllocCall->arg_end());

  CallInst *AllocWrapperCall = CallInst::Create(WrapperF, WrapperArgs);
  AllocWrapperCall->setCallingConv(WrapperF->getCallingConv());
  if (!AllocCall->use_empty()) {
    AllocCall->replaceAllUsesWith(AllocWrapperCall);
  }

  ReplaceInstWithInst(AllocCall, AllocWrapperCall);

  return AllocWrapperCall;
}

/// Sometimes \p malloc / \p calloc / \p realloc aren't called directly by a
/// program, but rather via a custom allocation wrapper function instead. For
/// these programs, we need to tag the calls to the custom allocation wrapper
/// function, rather than calls to \p malloc / \p calloc / \p realloc.
///
/// This means that the call site identifier is associated with the call to the
/// custom allocation wrapper function, rather than \p malloc / \p calloc / \p
/// realloc directly. When \p malloc / \p calloc / \p realloc is called by the
/// custom allocation wrapper function, the already-generated tag is passed
/// through to the fuzzalloc library.
Function *TagDynamicAlloc::tagAllocWrapperFunc(Function *OrigF,
                                               const TargetLibraryInfo *TLI) {
  FunctionType *OrigFTy = OrigF->getFunctionType();

  SmallVector<Type *, 4> TaggedFParams = {this->TagTy};
  TaggedFParams.insert(TaggedFParams.end(), OrigFTy->param_begin(),
                       OrigFTy->param_end());

  FunctionType *TaggedFTy = FunctionType::get(
      OrigFTy->getReturnType(), TaggedFParams, OrigFTy->isVarArg());
  Function *TaggedF =
      Function::Create(TaggedFTy, OrigF->getLinkage(),
                       "__tagged_" + OrigF->getName(), OrigF->getParent());
  TaggedF->setCallingConv(OrigF->getCallingConv());
  TaggedF->setAttributes(OrigF->getAttributes());

  // Skip the tag argument (i.e., first argument) in the tagged custom
  // allocation wrapper function
  ValueToValueMapTy VMap;
  auto NewFuncArgIt = TaggedF->arg_begin() + 1;
  for (auto &Arg : OrigF->args()) {
    VMap[&Arg] = &(*NewFuncArgIt++);
  }

  SmallVector<ReturnInst *, 8> Returns;
  CloneFunctionInto(TaggedF, OrigF, VMap, true, Returns);

  std::map<CallInst *, Function *> AllocCalls = getDynAllocCalls(TaggedF, TLI);

  Value *TagArg = TaggedF->arg_begin();
  for (auto &AllocCallWithWrapperF : AllocCalls) {
    CallInst *AllocCall = AllocCallWithWrapperF.first;
    Function *WrapperF = AllocCallWithWrapperF.second;

    tagDynAllocCall(AllocCall, WrapperF, TagArg);
  }

  return TaggedF;
}

void TagDynamicAlloc::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool TagDynamicAlloc::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);

  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  this->SizeTTy = DL.getIntPtrType(C);

  // Fuzzalloc's malloc/calloc/realloc functions take the same arguments as the
  // original dynamic allocation function, except that the first argument is a
  // tag that identifies the allocation site
  this->FuzzallocMallocF = checkFuzzallocFunction(M.getOrInsertFunction(
      TaggedMallocName, Int8PtrTy, this->TagTy, this->SizeTTy));
  this->FuzzallocCallocF = checkFuzzallocFunction(M.getOrInsertFunction(
      TaggedCallocName, Int8PtrTy, this->TagTy, this->SizeTTy, this->SizeTTy));
  this->FuzzallocReallocF = checkFuzzallocFunction(M.getOrInsertFunction(
      TaggedReallocName, Int8PtrTy, this->TagTy, Int8PtrTy, this->SizeTTy));

  return false;
}

bool TagDynamicAlloc::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  // Sometimes the PUT may use a custom memory allocator (that may or may not
  // wrap the standard malloc/calloc/realloc functions). The user can specify
  // the names of the custom memory allocator functions (as command-line args)
  // and calls to these functions will be tagged instead.
  //
  // Note that we have to do this here and not in `doInitialization` because we
  // cannot call `getAnalysis` in `doInitialization`
  Function *CustomMallocF = nullptr;
  Function *CustomCallocF = nullptr;
  Function *CustomReallocF = nullptr;

  if (!ClMallocWrapperName.empty()) {
    CustomMallocF = M.getFunction(ClMallocWrapperName);
    this->TaggedCustomMallocF = tagAllocWrapperFunc(CustomMallocF, TLI);
  }
  if (!ClCallocWrapperName.empty()) {
    CustomCallocF = M.getFunction(ClCallocWrapperName);
    this->TaggedCustomCallocF = tagAllocWrapperFunc(CustomCallocF, TLI);
  }
  if (!ClReallocWrapperName.empty()) {
    CustomReallocF = M.getFunction(ClReallocWrapperName);
    this->TaggedCustomReallocF = tagAllocWrapperFunc(CustomReallocF, TLI);
  }

  for (auto &F : M.functions()) {
    // Maps calls to malloc/calloc/realloc to the corresponding fuzzalloc
    // function
    std::map<CallInst *, Function *> AllocCalls = getDynAllocCalls(&F, TLI);

    for (auto &AllocCallWithWrapperF : AllocCalls) {
      CallInst *AllocCall = AllocCallWithWrapperF.first;
      Function *WrapperF = AllocCallWithWrapperF.second;

      // Generate a random tag representing the allocation site
      ConstantInt *Tag =
          ConstantInt::get(this->TagTy, RAND(DEFAULT_TAG + 1, TAG_MAX));

      tagDynAllocCall(AllocCall, WrapperF, Tag);
    }
  }

  // Delete the (untagged) custom dynamic allocation functions - they are now
  // no longer called
  if (CustomMallocF) {
    CustomMallocF->eraseFromParent();
  }
  if (CustomCallocF) {
    CustomCallocF->eraseFromParent();
  }
  if (CustomReallocF) {
    CustomReallocF->eraseFromParent();
  }

  return true;
}

static RegisterPass<TagDynamicAlloc>
    X("tag-dyn-alloc",
      "Tag dynamic allocation function calls and replace them with a call to "
      "fuzzalloc's wrapper functions",
      false, false);
