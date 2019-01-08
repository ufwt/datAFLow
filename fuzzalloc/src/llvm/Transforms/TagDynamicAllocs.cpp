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
/// original function calls are redirected to the fuzzalloc version.
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
                   "but via a custom wrapper function. These options let us "
                   "tag the wrapper function instead");

static cl::opt<std::string>
    ClMallocWrapperFuncName("malloc-wrapper-func",
                            cl::desc("Malloc wrapper function to tag"),
                            cl::cat(WrapperFuncCat));

static cl::opt<std::string>
    ClCallocWrapperFuncName("calloc-wrapper-func",
                            cl::desc("Calloc wrapper function to tag"),
                            cl::cat(WrapperFuncCat));

static cl::opt<std::string>
    ClReallocWrapperFuncName("realloc-wrapper-func",
                             cl::desc("Realloc wrapper function to tag"),
                             cl::cat(WrapperFuncCat));

STATISTIC(NumOfTaggedMalloc, "Number of malloc calls tagged.");
STATISTIC(NumOfTaggedCalloc, "Number of calloc calls tagged.");
STATISTIC(NumOfTaggedRealloc, "Number of realloc calls tagged.");

namespace {

static constexpr char *const TaggedMallocName = "__tagged_malloc";
static constexpr char *const TaggedCallocName = "__tagged_calloc";
static constexpr char *const TaggedReallocName = "__tagged_realloc";

/// TagDynamicAlloc: Tag dynamic memory allocation function calls (\p malloc,
/// \p calloc and \p realloc) with a randomly-generated identifier (to identify
/// their call site) and call the fuzzalloc function instead.
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
  Function *tagDynAllocWrapperFunc(Function *F, const TargetLibraryInfo *TLI);

public:
  static char ID;
  TagDynamicAlloc() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool doInitialization(Module &M) override;
  bool runOnModule(Module &M) override;
};

} // end anonymous namespace

// Adapted from llvm::checkSanitizerInterfaceFunction
static Function *checkFuzzallocFunc(Constant *FuncOrBitcast) {
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

/// Maps all dynamic memory allocation function calls in the function `F` to
/// the appropriate fuzzalloc function call.
std::map<CallInst *, Function *>
TagDynamicAlloc::getDynAllocCalls(Function *F, const TargetLibraryInfo *TLI) {
  std::map<CallInst *, Function *> AllocCalls;

  for (auto I = inst_begin(F); I != inst_end(F); ++I) {
    if (auto *Call = dyn_cast<CallInst>(&*I)) {
      auto *CalledFunc = Call->getCalledFunction();

      if (!ClMallocWrapperFuncName.empty() && CalledFunc &&
          CalledFunc->getName() == ClMallocWrapperFuncName) {
        AllocCalls.emplace(Call, this->TaggedCustomMallocF);
        NumOfTaggedMalloc++;
      } else if (isMallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocMallocF);
        NumOfTaggedMalloc++;
      } else if (!ClCallocWrapperFuncName.empty() && CalledFunc &&
                 CalledFunc->getName() == ClCallocWrapperFuncName) {
        AllocCalls.emplace(Call, this->TaggedCustomCallocF);
        NumOfTaggedCalloc++;
      } else if (isCallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocCallocF);
        NumOfTaggedCalloc++;
      } else if (!ClReallocWrapperFuncName.empty() && CalledFunc &&
                 CalledFunc->getName() == ClReallocWrapperFuncName) {
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

/// Tag dynamic memory allocation function calls with a call site identifier
/// (the `Tag` argument) and replace the call (the `Call` argument) with a call
/// to the appropriate fuzzalloc function (the `FuzzallocF` argument)
CallInst *TagDynamicAlloc::tagDynAllocCall(CallInst *Call, Function *FuzzallocF,
                                           Value *Tag) {
  // Copy the original allocation function call's arguments so that the tag is
  // the first argument passed to the fuzzalloc function
  SmallVector<Value *, 3> WrapperArgs = {Tag};
  WrapperArgs.insert(WrapperArgs.end(), Call->arg_begin(), Call->arg_end());

  CallInst *AllocWrapperCall = CallInst::Create(FuzzallocF, WrapperArgs);
  AllocWrapperCall->setCallingConv(FuzzallocF->getCallingConv());
  if (!Call->use_empty()) {
    Call->replaceAllUsesWith(AllocWrapperCall);
  }

  ReplaceInstWithInst(Call, AllocWrapperCall);

  return AllocWrapperCall;
}

/// Sometimes a PUT does not call a dynamic memory allocation function
/// directly, but rather via a custom allocation wrapper function. For these
/// PUTs, we must tag the calls to the custom allocation wrapper function (the
/// `OrigF` argument), rather than the underlying \p malloc / \p calloc /
/// \p realloc call.
///
/// This means that the call site identifier is now associated with the call to
/// the custom allocation wrapper function, rather than the underlying
/// \p malloc / \p calloc / \p realloc call. When \p malloc / \p calloc /
/// \p realloc is eventually (if at all) called by the custom allocation
/// wrapper function, the already-generated tag is passed through to the
/// appropriate fuzzalloc function.
Function *
TagDynamicAlloc::tagDynAllocWrapperFunc(Function *OrigF,
                                        const TargetLibraryInfo *TLI) {
  assert(OrigF && "Custom allocation wrapper function is null");
  FunctionType *OrigFTy = OrigF->getFunctionType();

  // Add the tag (i.e., call site identifier) as the first argument to the
  // custom allocation wrapper function
  SmallVector<Type *, 4> TaggedFParams = {this->TagTy};
  TaggedFParams.insert(TaggedFParams.end(), OrigFTy->param_begin(),
                       OrigFTy->param_end());

  FunctionType *TaggedFTy = FunctionType::get(
      OrigFTy->getReturnType(), TaggedFParams, OrigFTy->isVarArg());

  // Make a new version of the custom allocation wrapper function, with
  // "__tagged_" preprended to the name and that accepts a tag as the first
  // argument to the function
  Function *TaggedF =
      Function::Create(TaggedFTy, OrigF->getLinkage(),
                       "__tagged_" + OrigF->getName(), OrigF->getParent());
  TaggedF->setCallingConv(OrigF->getCallingConv());
  TaggedF->setAttributes(OrigF->getAttributes());

  // Map the original function arguments to the new version of the custom
  // allocation wrapper function. Skip the tag argument (i.e., first argument)
  ValueToValueMapTy VMap;
  auto NewFuncArgIt = TaggedF->arg_begin() + 1;
  for (auto &Arg : OrigF->args()) {
    VMap[&Arg] = &(*NewFuncArgIt++);
  }

  SmallVector<ReturnInst *, 8> Returns;
  CloneFunctionInto(TaggedF, OrigF, VMap, true, Returns);

  // Get all the dynamic memory allocation function calls that this custom
  // allocation wrapper function makes and replace them with a call to the
  // appropriate fuzzalloc function
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
  // original dynamic memory allocation function, except that the first
  // argument is a tag that identifies the allocation site
  this->FuzzallocMallocF = checkFuzzallocFunc(M.getOrInsertFunction(
      TaggedMallocName, Int8PtrTy, this->TagTy, this->SizeTTy));
  this->FuzzallocCallocF = checkFuzzallocFunc(M.getOrInsertFunction(
      TaggedCallocName, Int8PtrTy, this->TagTy, this->SizeTTy, this->SizeTTy));
  this->FuzzallocReallocF = checkFuzzallocFunc(M.getOrInsertFunction(
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

  if (!ClMallocWrapperFuncName.empty()) {
    CustomMallocF = M.getFunction(ClMallocWrapperFuncName);
    this->TaggedCustomMallocF = tagDynAllocWrapperFunc(CustomMallocF, TLI);
  }
  if (!ClCallocWrapperFuncName.empty()) {
    CustomCallocF = M.getFunction(ClCallocWrapperFuncName);
    this->TaggedCustomCallocF = tagDynAllocWrapperFunc(CustomCallocF, TLI);
  }
  if (!ClReallocWrapperFuncName.empty()) {
    CustomReallocF = M.getFunction(ClReallocWrapperFuncName);
    this->TaggedCustomReallocF = tagDynAllocWrapperFunc(CustomReallocF, TLI);
  }

  for (auto &F : M.functions()) {
    // Maps calls to malloc/calloc/realloc to the appropriate fuzzalloc
    // function (__tagged_malloc, __tagged_calloc, and __tagged_realloc
    // respectively)
    std::map<CallInst *, Function *> AllocCalls = getDynAllocCalls(&F, TLI);

    // Tag all of the dynamic allocation function calls with a random value
    // that represents the allocation site
    for (auto &AllocCallWithWrapperF : AllocCalls) {
      CallInst *AllocCall = AllocCallWithWrapperF.first;
      Function *WrapperF = AllocCallWithWrapperF.second;

      // Generate the random tag
      ConstantInt *Tag =
          ConstantInt::get(this->TagTy, RAND(DEFAULT_TAG + 1, TAG_MAX));

      tagDynAllocCall(AllocCall, WrapperF, Tag);
    }
  }

  // Delete the (untagged) custom dynamic allocation wrapper functions - they
  // are now no longer called
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
      "the appropriate fuzzalloc function",
      false, false);
