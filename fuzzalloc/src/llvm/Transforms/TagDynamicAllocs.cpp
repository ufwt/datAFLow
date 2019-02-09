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
/// \p malloc, \p calloc, etc.) with a randomly-generated identifier that is
/// understood by the fuzzalloc memory allocation library. The original
/// function calls are redirected to their corresponding fuzzalloc version.
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
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "tag-dyn-alloc"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

static cl::opt<bool> ClRandomTags("random-tags",
                                  cl::desc("Generate tags randomly"));

static cl::opt<std::string>
    ClWhitelist("alloc-whitelist",
                cl::desc("Path to memory allocation whitelist file"));

STATISTIC(NumOfTaggedCalls,
          "Number of tagged dynamic memory allocation function calls.");

namespace {

static constexpr char *const FuzzallocMallocFuncName = "__tagged_malloc";
static constexpr char *const FuzzallocCallocFuncName = "__tagged_calloc";
static constexpr char *const FuzzallocReallocFuncName = "__tagged_realloc";

/// Whitelist of custom dynamic memory allocation wrapper functions
class FuzzallocWhitelist {
private:
  std::unique_ptr<SpecialCaseList> SCL;

public:
  FuzzallocWhitelist() = default;

  FuzzallocWhitelist(std::unique_ptr<SpecialCaseList> List)
      : SCL(std::move(List)){};

  bool isIn(const Function &F) const {
    return SCL && SCL->inSection("fuzzalloc", "fun", F.getName());
  }
};

/// TagDynamicAlloc: Tag dynamic memory allocation function calls (\p malloc,
/// \p calloc and \p realloc) with a randomly-generated identifier (to identify
/// their call site) and call the fuzzalloc function instead
class TagDynamicAlloc : public ModulePass {
private:
  Function *FuzzallocMallocF;
  Function *FuzzallocCallocF;
  Function *FuzzallocReallocF;

  FuzzallocWhitelist Whitelist;

  IntegerType *TagTy;
  IntegerType *SizeTTy;

  FunctionType *translateTaggedFuncType(FunctionType *);
  Function *translateTaggedFunc(Function *);
  std::map<CallInst *, Function *> getDynAllocCalls(Function *,
                                                    const TargetLibraryInfo *);
  CallInst *tagDynAllocCall(CallInst *, Function *, Value *);
  Function *tagDynAllocFunc(Function *, const TargetLibraryInfo *);
  GlobalAlias *tagDynAllocGlobalAlias(GlobalAlias *);

public:
  static char ID;
  TagDynamicAlloc() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
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

static FuzzallocWhitelist getWhitelist() {
  if (ClWhitelist.empty()) {
    return FuzzallocWhitelist();
  }

  if (!sys::fs::exists(ClWhitelist)) {
    std::string Err;
    raw_string_ostream Stream(Err);
    Stream << "fuzzalloc whitelist does not exist at " << ClWhitelist;
    report_fatal_error(Err);
  }

  return FuzzallocWhitelist(SpecialCaseList::createOrDie({ClWhitelist}));
}

char TagDynamicAlloc::ID = 0;

/// Translates a function type to its tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// to the given function type.
FunctionType *TagDynamicAlloc::translateTaggedFuncType(FunctionType *OrigFTy) {
  SmallVector<Type *, 4> TaggedFParams = {this->TagTy};
  TaggedFParams.insert(TaggedFParams.end(), OrigFTy->param_begin(),
                       OrigFTy->param_end());

  return FunctionType::get(OrigFTy->getReturnType(), TaggedFParams,
                           OrigFTy->isVarArg());
}

/// Translates a function to its tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// and prepends the function name with "__tagged_".
Function *TagDynamicAlloc::translateTaggedFunc(Function *OrigF) {
  FunctionType *NewFTy = translateTaggedFuncType(OrigF->getFunctionType());
  Twine NewFName = "__tagged_" + OrigF->getName();

  auto *NewF = OrigF->getParent()->getOrInsertFunction(NewFName.str(), NewFTy);
  assert(isa<Function>(NewF) && "Translated tagged function not a function");

  return cast<Function>(NewF);
}

/// Maps all dynamic memory allocation function calls in the function `F` to
/// the appropriate tagged function call.
///
/// For example, `malloc` maps to `__tagged_malloc`, while functions listed in
/// the fuzzalloc whitelist are mapped to their tagged versions.
std::map<CallInst *, Function *>
TagDynamicAlloc::getDynAllocCalls(Function *F, const TargetLibraryInfo *TLI) {
  std::map<CallInst *, Function *> AllocCalls;

  for (auto I = inst_begin(F); I != inst_end(F); ++I) {
    if (auto *Call = dyn_cast<CallInst>(&*I)) {
      // It's not going to be an intrinsic
      if (isa<IntrinsicInst>(Call)) {
        continue;
      }

      // The result of a dynamic memory allocation function call is typically
      // cast. To determine the actual function being called, we must remove
      // these casts
      auto *CalledValue = Call->getCalledValue()->stripPointerCasts();

      if (isMallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocMallocF);
        NumOfTaggedCalls++;
      } else if (isCallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocCallocF);
        NumOfTaggedCalls++;
      } else if (isReallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocReallocF);
        NumOfTaggedCalls++;
      } else if (auto *CalledFunc = dyn_cast<Function>(CalledValue)) {
        if (this->Whitelist.isIn(*CalledFunc)) {
          AllocCalls.emplace(Call, translateTaggedFunc(CalledFunc));
          NumOfTaggedCalls++;
        }
      }
    }
  }

  return AllocCalls;
}

/// Tag dynamic memory allocation function calls with a call site identifier
/// (the `Tag` argument) and replace the call (the `Call` argument) with a call
/// to the appropriate tagged function (the `TaggedF` argument)
CallInst *TagDynamicAlloc::tagDynAllocCall(CallInst *OrigCall,
                                           Function *TaggedF, Value *Tag) {
  // Copy the original allocation function call's arguments so that the tag is
  // the first argument passed to the tagged function
  SmallVector<Value *, 3> FuzzallocArgs = {Tag};
  FuzzallocArgs.insert(FuzzallocArgs.end(), OrigCall->arg_begin(),
                       OrigCall->arg_end());

  Value *FuzzallocCallee;
  IRBuilder<> IRB(OrigCall);

  if (auto *ConstExpr = dyn_cast<ConstantExpr>(OrigCall->getCalledValue())) {
    // Sometimes the result of the original dynamic memory allocation function
    // call is cast to some other pointer type. Because this is a function
    // call, the underlying type should still be a FunctionType (which we check
    // in the various asserts)

    // I think that we can safely assume that function calls to dynamic memory
    // allocation functions can only be bitcast
    auto *BitCast = cast<BitCastInst>(ConstExpr->getAsInstruction());

    assert(isa<FunctionType>(BitCast->getDestTy()->getPointerElementType()) &&
           "Must be bitcast of a function call");
    Type *OrigBitCastTy = BitCast->getDestTy()->getPointerElementType();

    // Add the tag (i.e., the call site identifier) as the first argument to
    // the cast function type
    Type *NewBitCastTy =
        translateTaggedFuncType(cast<FunctionType>(OrigBitCastTy));

    // The callee is a cast version of the tagged function
    FuzzallocCallee = IRB.CreateBitCast(TaggedF, NewBitCastTy->getPointerTo());

    // getAsInstruction() leaves the instruction floating around and unattached
    // to anything, so we must manually delete it
    delete BitCast;
  } else {
    // The function call result was not cast, so there is no need to do
    // anything to the callee
    FuzzallocCallee = TaggedF;
  }

  auto *FuzzallocCall = IRB.CreateCall(FuzzallocCallee, FuzzallocArgs);
  FuzzallocCall->setCallingConv(TaggedF->getCallingConv());

  // Replace the original dynamic memory allocation function call
  OrigCall->replaceAllUsesWith(FuzzallocCall);
  OrigCall->eraseFromParent();

  return FuzzallocCall;
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
///
/// A whitelist of custom allocation wrapper functions can be passed through a
/// command-line argument.
Function *TagDynamicAlloc::tagDynAllocFunc(Function *OrigF,
                                           const TargetLibraryInfo *TLI) {
  // Make a new version of the custom allocation wrapper function, with
  // "__tagged_" preprended to the name and that accepts a tag as the first
  // argument to the function
  Function *TaggedF = translateTaggedFunc(OrigF);

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
  // appropriate tagged function (which may a fuzzalloc function or another
  // whitelisted function)
  std::map<CallInst *, Function *> AllocCalls = getDynAllocCalls(TaggedF, TLI);

  Value *TagArg = TaggedF->arg_begin();
  for (auto &CallWithTaggedF : AllocCalls) {
    tagDynAllocCall(CallWithTaggedF.first, CallWithTaggedF.second, TagArg);
  }

  return TaggedF;
}

/// A dynamic memory allocation function could be assigned to a global alias.
/// If so, the global alias must be updated to point to a tagged version of the
/// dynamic memory allocation function.
GlobalAlias *TagDynamicAlloc::tagDynAllocGlobalAlias(GlobalAlias *OrigGA) {
  Constant *Aliasee = OrigGA->getAliasee();

  assert(isa<Function>(Aliasee) && "The aliasee must be a function");
  auto *NewF = translateTaggedFunc(cast<Function>(Aliasee));

  auto *NewGA = GlobalAlias::create(
      NewF->getType()->getPointerElementType(),
      NewF->getType()->getAddressSpace(), OrigGA->getLinkage(),
      "__tagged_" + OrigGA->getName(), NewF, OrigGA->getParent());

  // TODO handle users
  assert(OrigGA->getNumUses() == 0);

  return NewGA;
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

  this->Whitelist = getWhitelist();

  // Fuzzalloc's malloc/calloc/realloc functions take the same arguments as the
  // original dynamic memory allocation function, except that the first
  // argument is a tag that identifies the allocation site
  this->FuzzallocMallocF = checkFuzzallocFunc(M.getOrInsertFunction(
      FuzzallocMallocFuncName, Int8PtrTy, this->TagTy, this->SizeTTy));
  this->FuzzallocCallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocCallocFuncName, Int8PtrTy, this->TagTy,
                            this->SizeTTy, this->SizeTTy));
  this->FuzzallocReallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocReallocFuncName, Int8PtrTy, this->TagTy,
                            Int8PtrTy, this->SizeTTy));

  return false;
}

bool TagDynamicAlloc::runOnModule(Module &M) {
  LLVMContext &C = M.getContext();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  SmallVector<Function *, 8> FuncsToDelete;

  // Replace whitelisted memory allocation functions with a tagged version.
  // Mark the original function for deletion
  for (auto &F : M.functions()) {
    if (this->Whitelist.isIn(F)) {
      tagDynAllocFunc(&F, TLI);
      FuncsToDelete.push_back(&F);
    }
  }

  tag_t TagVal = DEFAULT_TAG;

  for (auto &F : M.functions()) {
    // Maps malloc/calloc/realloc calls to the appropriate fuzzalloc
    // function (__tagged_malloc, __tagged_calloc, and __tagged_realloc
    // respectively), as well as whitelisted function calls to their tagged
    // versions
    std::map<CallInst *, Function *> AllocCalls = getDynAllocCalls(&F, TLI);

    // Tag all of the dynamic allocation function calls with an integer value
    // that represents the allocation site
    for (auto &CallWithTaggedF : AllocCalls) {
      // Either generate a random tag or increment a counter
      TagVal = ClRandomTags ? RAND(DEFAULT_TAG + 1, TAG_MAX) : TagVal + 1;

      // Generate the tag
      ConstantInt *Tag = ConstantInt::get(this->TagTy, TagVal);

      // Tag the function call
      tagDynAllocCall(CallWithTaggedF.first, CallWithTaggedF.second, Tag);
    }
  }

  // Check the users of the functions to delete. If a function is assigned to
  // a global variable, rewrite the global variable with a tagged version
  for (auto *F : FuncsToDelete) {
    for (auto *U : F->users()) {
      if (auto *GA = dyn_cast<GlobalAlias>(U)) {
        tagDynAllocGlobalAlias(GA);
        GA->eraseFromParent();
      }
    }
  }

  // Delete the old (untagged) memory allocation functions. Make sure that the
  // only users are call instructions
  for (auto *F : FuncsToDelete) {
    for (auto *U : F->users()) {
      U->dropAllReferences();
    }
    F->eraseFromParent();
  }

  return true;
}

static RegisterPass<TagDynamicAlloc>
    X("tag-dyn-alloc",
      "Tag dynamic allocation function calls and replace them with a call to "
      "the appropriate fuzzalloc function",
      false, false);
