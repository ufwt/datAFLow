//===-- TagDynamicAllocss.cpp - Tag dynamic memory allocs with a unique ID
//-===//
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

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "fuzzalloc.h"

using namespace llvm;

#define DEBUG_TYPE "tag-dyn-allocs"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

static cl::opt<std::string>
    ClWhitelist("fuzzalloc-whitelist",
                cl::desc("Path to memory allocation whitelist file"));

STATISTIC(NumOfTaggedCalls,
          "Number of tagged dynamic memory allocation function calls.");

namespace {

static const char *const FuzzallocMallocFuncName = "__tagged_malloc";
static const char *const FuzzallocCallocFuncName = "__tagged_calloc";
static const char *const FuzzallocReallocFuncName = "__tagged_realloc";

/// Whitelist of dynamic memory allocation wrapper functions and global
/// variables
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

  bool isIn(const GlobalVariable &GV) const {
    return SCL && SCL->inSection("fuzzalloc", "gv", GV.getName());
  }
};

/// TagDynamicAllocs: Tag dynamic memory allocation function calls (\p malloc,
/// \p calloc and \p realloc) with a randomly-generated identifier (to identify
/// their call site) and call the fuzzalloc function instead
class TagDynamicAllocs : public ModulePass {
private:
  Function *FuzzallocMallocF;
  Function *FuzzallocCallocF;
  Function *FuzzallocReallocF;

  FuzzallocWhitelist Whitelist;

  IntegerType *TagTy;
  IntegerType *SizeTTy;

  SmallPtrSet<Function *, 8> TaggedFuncs;

  FunctionType *translateTaggedFunctionType(FunctionType *) const;
  Function *translateTaggedFunction(Function *) const;
  GlobalVariable *translateTaggedGlobalVariable(GlobalVariable *) const;

  std::map<CallInst *, Function *> getDynAllocCalls(Function *,
                                                    const TargetLibraryInfo *);

  Function *tagFunction(Function *, const TargetLibraryInfo *);
  GlobalVariable *tagGlobalVariable(GlobalVariable *);
  GlobalAlias *tagGlobalAlias(GlobalAlias *);

public:
  static char ID;
  TagDynamicAllocs() : ModulePass(ID) {}

  ConstantInt *generateTag() const;
  bool functionTagged(const Function *) const;
  CallInst *tagCall(CallInst *, Value *, Value *) const;

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
};

class GlobalVariableRewriter : public PtrUseVisitor<GlobalVariableRewriter> {
  friend class PtrUseVisitor<GlobalVariableRewriter>;
  friend class InstVisitor<GlobalVariableRewriter>;

private:
  Type *TaggedTy;
  LoadInst *OrigLoad;
  LoadInst *NewLoad;
  TagDynamicAllocs *Pass;

public:
  GlobalVariableRewriter(LoadInst *Load, GlobalVariable *TaggedGV,
                         TagDynamicAllocs *Pass)
      : PtrUseVisitor<GlobalVariableRewriter>(
            TaggedGV->getParent()->getDataLayout()),
        TaggedTy(TaggedGV->getValueType()), OrigLoad(Load),
        // Load the global variable containing the tagged function
        NewLoad(new LoadInst(TaggedGV, "", Load->isVolatile(),
                             Load->getAlignment(), Load->getOrdering(),
                             Load->getSyncScopeID(), Load)),
        Pass(Pass) {}

private:
  /// Check that the type of the given value matches the type of the tagged
  /// global variable.
  bool isTaggedType(const Value *V) const {
    return V->getType() == this->TaggedTy;
  }

  /// Replace a call to the function stored in the original global variable
  /// with a call to the tagged version. The value of the tag depends on where
  /// the function call is occurring: if the tagged function is being called
  /// from within another tagged function, just pass the tag argument straight
  /// through. Otherwise, generate a new tag.
  CallInst *tagCall(CallInst *Call, Value *Callee) {
    auto *ParentF = Call->getFunction();
    Value *Tag = this->Pass->functionTagged(ParentF)
                     ? ParentF->arg_begin()
                     : static_cast<Value *>(this->Pass->generateTag());

    return this->Pass->tagCall(Call, Callee, Tag);
  }

  void visitCallInst(CallInst &CI) {
    auto *TaggedCall = tagCall(&CI, this->NewLoad);

    // Replace the original dynamic memory allocation function call
    CI.replaceAllUsesWith(TaggedCall);
    CI.eraseFromParent();
  }

  void visitPHINode(PHINode &PN) {
    PN.replaceUsesOfWith(this->OrigLoad, this->NewLoad);

    // Once all of the values in the PHI node are of the same type as the
    // tagged global variable, we can replace the PHI node
    if (std::all_of(PN.value_op_begin(), PN.value_op_end(),
                    [this](const Value *Val) { return isTaggedType(Val); })) {
      // Replace the PHI node with an equivalent node of the correct type
      // (i.e., so that it matches the type of the tagged global variable)
      auto *NewPN =
          PHINode::Create(this->TaggedTy, PN.getNumIncomingValues(),
                          PN.hasName() ? "__tagged_" + PN.getName() : "", &PN);
      for (unsigned i = 0; i < PN.getNumIncomingValues(); ++i) {
        NewPN->addIncoming(PN.getIncomingValue(i), PN.getIncomingBlock(i));
      }

      // Cannot use `replaceAllUsesWith` because the PHI nodes have different
      // types
      for (auto &U : PN.uses()) {
        U.set(NewPN);
      }

      // Nothing uses the PHI node now. Delete it
      PN.eraseFromParent();

      // Cache users
      // SmallVector<User *, 4> Users(NewPN->user_begin(), NewPN->user_end());

      for (auto *U : NewPN->users()) {
        // TODO only deal with call instructions for now
        assert(isa<CallInst>(U));
        auto *OrigCall = cast<CallInst>(U);
        auto *TaggedCall = tagCall(OrigCall, NewPN);

        // Replace the original dynamic memory allocation function call
        OrigCall->replaceAllUsesWith(TaggedCall);
        OrigCall->eraseFromParent();
      }
    }
  }

  void visitInstruction(Instruction &I) { PI.setAborted(&I); }
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

char TagDynamicAllocs::ID = 0;

/// Return true if the given function has a tagged version.
bool TagDynamicAllocs::functionTagged(const Function *F) const {
  return this->TaggedFuncs.find(F) != this->TaggedFuncs.end();
}

/// Generate a random tag
ConstantInt *TagDynamicAllocs::generateTag() const {
  return ConstantInt::get(this->TagTy, RAND(DEFAULT_TAG + 1, TAG_MAX));
}

/// Translates a function type to its tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// to the given function type.
FunctionType *
TagDynamicAllocs::translateTaggedFunctionType(FunctionType *OrigFTy) const {
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
Function *TagDynamicAllocs::translateTaggedFunction(Function *OrigF) const {
  FunctionType *NewFTy = translateTaggedFunctionType(OrigF->getFunctionType());
  Twine NewFName = "__tagged_" + OrigF->getName();

  auto *NewF = OrigF->getParent()->getOrInsertFunction(NewFName.str(), NewFTy);
  assert(isa<Function>(NewF) && "Translated tagged function not a function");

  return cast<Function>(NewF);
}

/// Translate a dynamic allocation function stored in a global variable to its
/// tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// of the function and prepends the global variable name with "__tagged_".
GlobalVariable *
TagDynamicAllocs::translateTaggedGlobalVariable(GlobalVariable *OrigGV) const {
  FunctionType *NewGVTy = translateTaggedFunctionType(
      cast<FunctionType>(OrigGV->getValueType()->getPointerElementType()));
  Twine NewGVName = "__tagged_" + OrigGV->getName();

  auto *NewGV = OrigGV->getParent()->getOrInsertGlobal(NewGVName.str(),
                                                       NewGVTy->getPointerTo());
  assert(isa<GlobalVariable>(NewGV) &&
         "Translated tagged global variable not a global variable");

  return cast<GlobalVariable>(NewGV);
}

/// Maps all dynamic memory allocation function calls in the function `F` to
/// the appropriate tagged function call.
///
/// For example, `malloc` maps to `__tagged_malloc`, while functions listed in
/// the fuzzalloc whitelist are mapped to their tagged versions.
std::map<CallInst *, Function *>
TagDynamicAllocs::getDynAllocCalls(Function *F, const TargetLibraryInfo *TLI) {
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
      } else if (isCallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocCallocF);
      } else if (isReallocLikeFn(Call, TLI)) {
        AllocCalls.emplace(Call, this->FuzzallocReallocF);
      } else if (auto *CalledFunc = dyn_cast<Function>(CalledValue)) {
        if (this->Whitelist.isIn(*CalledFunc)) {
          AllocCalls.emplace(Call, translateTaggedFunction(CalledFunc));
        }
      }
    }
  }

  return AllocCalls;
}

/// Replace a function call (`OrigCall`) with a call to `NewCallee` that is
/// tagged with an allocation site identifier stored in `Tag`.
CallInst *TagDynamicAllocs::tagCall(CallInst *OrigCall, Value *NewCallee,
                                    Value *Tag) const {
  // Copy the original allocation function call's arguments so that the tag is
  // the first argument passed to the tagged function
  SmallVector<Value *, 3> FuzzallocArgs = {Tag};
  FuzzallocArgs.insert(FuzzallocArgs.end(), OrigCall->arg_begin(),
                       OrigCall->arg_end());

  Value *CastNewCallee;
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
        translateTaggedFunctionType(cast<FunctionType>(OrigBitCastTy));

    // The callee is a cast version of the tagged function
    CastNewCallee = IRB.CreateBitCast(NewCallee, NewBitCastTy->getPointerTo());

    // getAsInstruction() leaves the instruction floating around and unattached
    // to anything, so we must manually delete it
    delete BitCast;
  } else {
    // The function call result was not cast, so there is no need to do
    // anything to the callee
    CastNewCallee = NewCallee;
  }

  // Create the call to the callee
  NumOfTaggedCalls++;
  return IRB.CreateCall(CastNewCallee, FuzzallocArgs,
                        OrigCall->hasName() ? "__tagged_" + OrigCall->getName()
                                            : "");
}

/// Sometimes a PUT does not call a dynamic memory allocation function
/// directly, but rather via a allocation wrapper function. For these PUTs, we
/// must tag the calls to the allocation wrapper function (the `OrigF`
/// argument), rather than the underlying \p malloc / \p calloc / \p realloc
/// call.
///
/// This means that the call site identifier is now associated with the call to
/// the allocation wrapper function, rather than the underlying \p malloc /
/// \p calloc / \p realloc call. When \p malloc / \p calloc / \p realloc is
/// eventually (if at all) called by the allocation wrapper function, the
/// already-generated tag is passed through to the appropriate fuzzalloc
/// function.
///
/// A whitelist of allocation wrapper functions can be passed through as a
/// command-line argument.
Function *TagDynamicAllocs::tagFunction(Function *OrigF,
                                        const TargetLibraryInfo *TLI) {
  // Make a new version of the allocation wrapper function, with "__tagged_"
  // preprended to the name and that accepts a tag as the first argument to the
  // function
  Function *TaggedF = translateTaggedFunction(OrigF);

  // We can only replace the function body if it is defined in this module
  if (!OrigF->isDeclaration()) {
    // Map the original function arguments to the new version of the allocation
    // wrapper function. Skip the tag argument (i.e., first argument)
    ValueToValueMapTy VMap;
    auto NewFuncArgIt = TaggedF->arg_begin() + 1;
    for (auto &Arg : OrigF->args()) {
      VMap[&Arg] = &(*NewFuncArgIt++);
    }

    SmallVector<ReturnInst *, 8> Returns;
    CloneFunctionInto(TaggedF, OrigF, VMap, true, Returns);

    // Get all the dynamic memory allocation function calls that the allocation
    // wrapper function makes and replace them with a call to the appropriate
    // tagged function (which may be a fuzzalloc function or another
    // whitelisted function)
    std::map<CallInst *, Function *> AllocCalls =
        getDynAllocCalls(TaggedF, TLI);

    Value *TagArg = TaggedF->arg_begin();
    for (auto &CallWithTaggedF : AllocCalls) {
      auto *TaggedCall =
          tagCall(CallWithTaggedF.first, CallWithTaggedF.second, TagArg);

      // Replace the original dynamic memory allocation function call
      CallWithTaggedF.first->replaceAllUsesWith(TaggedCall);
      CallWithTaggedF.first->eraseFromParent();
    }
  }

  return TaggedF;
}

/// A dynamic memory allocation function could be assigned to a global
/// variable (which is different to a global alias). If so, the global variable
/// must be updated to point to a tagged version of the dynamic memory
/// allocation function.
///
/// A whitelist of global variables containing allocation wrapper functions can
/// be passed through as a command-line argument.
GlobalVariable *TagDynamicAllocs::tagGlobalVariable(GlobalVariable *OrigGV) {
  GlobalVariable *TaggedGV = translateTaggedGlobalVariable(OrigGV);

  // Replace the initializer (if it exists) with a tagged version
  if (OrigGV->hasInitializer()) {
    auto *OrigInitializer = OrigGV->getInitializer();

    // The initializer must be a function because we are only interested in
    // global variables that point to dynamic memory allocation functions
    assert(isa<Function>(OrigInitializer) &&
           "The initializer must be a function");
    TaggedGV->setInitializer(
        translateTaggedFunction(cast<Function>(OrigInitializer)));
  }

  // Replace all the users of the global variable. Currently we only deal with
  // loads
  for (auto *U : OrigGV->users()) {
    if (auto *Load = dyn_cast<LoadInst>(U)) {
      GlobalVariableRewriter GVRewriter(Load, TaggedGV, this);
      GVRewriter.visitPtr(*Load);

      Load->eraseFromParent();
    }
  }

  return TaggedGV;
}

/// A dynamic memory allocation function could be assigned to a global alias.
/// If so, the global alias must be updated to point to a tagged version of the
/// dynamic memory allocation function.
GlobalAlias *TagDynamicAllocs::tagGlobalAlias(GlobalAlias *OrigGA) {
  Constant *OrigAliasee = OrigGA->getAliasee();
  Constant *NewAliasee;

  if (auto *AliaseeF = dyn_cast<Function>(OrigAliasee)) {
    NewAliasee = translateTaggedFunction(AliaseeF);
  } else if (auto *AliaseeGV = dyn_cast<GlobalVariable>(OrigAliasee)) {
    NewAliasee = translateTaggedGlobalVariable(AliaseeGV);
  } else {
    assert(false && "The aliasee must be a function or global variable");
  }

  auto *NewGA = GlobalAlias::create(
      NewAliasee->getType()->getPointerElementType(),
      NewAliasee->getType()->getPointerAddressSpace(), OrigGA->getLinkage(),
      OrigGA->hasName() ? "__tagged_" + OrigGA->getName() : "", NewAliasee,
      OrigGA->getParent());

  // TODO handle users
  assert(OrigGA->getNumUses() == 0 && "Not yet implemented");

  return NewGA;
}

void TagDynamicAllocs::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool TagDynamicAllocs::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  this->SizeTTy = DL.getIntPtrType(C);

  this->Whitelist = getWhitelist();

  return false;
}

bool TagDynamicAllocs::runOnModule(Module &M) {
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  PointerType *Int8PtrTy = Type::getInt8PtrTy(M.getContext());

  // Create the tagged memory allocation functions. These functions take the
  // take the same arguments as the original dynamic memory allocation
  // function, except that the first argument is a tag that identifies the
  // allocation site
  this->FuzzallocMallocF = checkFuzzallocFunc(M.getOrInsertFunction(
      FuzzallocMallocFuncName, Int8PtrTy, this->TagTy, this->SizeTTy));
  this->TaggedFuncs.insert(this->FuzzallocMallocF);

  this->FuzzallocCallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocCallocFuncName, Int8PtrTy, this->TagTy,
                            this->SizeTTy, this->SizeTTy));
  this->TaggedFuncs.insert(this->FuzzallocCallocF);

  this->FuzzallocReallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocReallocFuncName, Int8PtrTy, this->TagTy,
                            Int8PtrTy, this->SizeTTy));
  this->TaggedFuncs.insert(FuzzallocReallocF);

  // Replace whitelisted memory allocation functions with a tagged version.
  // Mark the original function for deletion
  SmallVector<Function *, 8> FuncsToDelete = {M.getFunction("malloc"),
                                              M.getFunction("calloc"),
                                              M.getFunction("realloc")};

  for (auto &F : M.functions()) {
    if (this->Whitelist.isIn(F)) {
      auto *TaggedF = tagFunction(&F, TLI);

      this->TaggedFuncs.insert(TaggedF);
      FuncsToDelete.push_back(&F);
    }
  }

  // Collect and tag function calls as required
  std::map<CallInst *, Function *> AllocCalls;

  for (auto &F : M.functions()) {
    AllocCalls.clear();

    // Maps malloc/calloc/realloc calls to the appropriate fuzzalloc function
    // (__tagged_malloc, __tagged_calloc, and __tagged_realloc respectively),
    // as well as whitelisted function calls, to their tagged versions
    AllocCalls = getDynAllocCalls(&F, TLI);

    // Tag all of the dynamic allocation function calls with an integer value
    // that represents the allocation site
    for (auto &CallWithTaggedF : AllocCalls) {
      auto *TaggedCall =
          tagCall(CallWithTaggedF.first, CallWithTaggedF.second, generateTag());

      // Replace the original dynamic memory allocation function call
      CallWithTaggedF.first->replaceAllUsesWith(TaggedCall);
      CallWithTaggedF.first->eraseFromParent();
    }
  }

  // Replace whitelisted global variables (that presumably store a pointer to a
  // memory alloation function) with a tagged version. Mark the original global
  // variable for deletion
  SmallVector<GlobalVariable *, 8> GVsToDelete;

  for (auto &GV : M.globals()) {
    if (this->Whitelist.isIn(GV)) {
      tagGlobalVariable(&GV);
      GVsToDelete.push_back(&GV);
    }
  }

  // Replace global aliases pointing to whitelisted functions/global variables
  // to point to a tagged function/global variable instead. Mark the original
  // alias for deletion
  SmallVector<GlobalAlias *, 8> GAsToDelete;

  for (auto &GA : M.aliases()) {
    if (auto *AliaseeF = dyn_cast<Function>(GA.getAliasee())) {
      if (this->Whitelist.isIn(*AliaseeF)) {
        tagGlobalAlias(&GA);
        GAsToDelete.push_back(&GA);
      }
    }
  }

  // Delete all the things marked for deletion

  for (auto *GA : GAsToDelete) {
    GA->replaceAllUsesWith(UndefValue::get(GA->getType()));
    GA->eraseFromParent();
  }

  for (auto *GV : GVsToDelete) {
    GV->replaceAllUsesWith(UndefValue::get(GV->getType()));
    GV->eraseFromParent();
  }

  for (auto *F : FuncsToDelete) {
    if (!F) {
      continue;
    }

    F->replaceAllUsesWith(UndefValue::get(F->getType()));
    F->eraseFromParent();
  }

  return true;
}

static RegisterPass<TagDynamicAllocs>
    X("tag-dyn-allocs",
      "Tag dynamic allocation function calls and replace them with a call to "
      "the appropriate fuzzalloc function",
      false, false);

static void registerTagDynamicAllocsPass(const PassManagerBuilder &,
                                         legacy::PassManagerBase &PM) {
  PM.add(new TagDynamicAllocs());
}

static RegisterStandardPasses
    RegisterTagDynamicAllocsPass(PassManagerBuilder::EP_OptimizerLast,
                                 registerTagDynamicAllocsPass);

static RegisterStandardPasses
    RegisterTagDynamicAllocsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                                  registerTagDynamicAllocsPass);
