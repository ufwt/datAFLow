//===-- TagDynamicAllocss.cpp - Tag dynamic memory allocs with unique ID --===//
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
#include <set>

#include <stdint.h>
#include <stdlib.h>

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/IndirectCallSiteVisitor.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "debug.h"     // from AFL
#include "fuzzalloc.h" // from fuzzalloc

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-tag-dyn-allocs"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

static cl::opt<std::string>
    ClWhitelist("fuzzalloc-whitelist",
                cl::desc("Path to memory allocation whitelist file"));

STATISTIC(NumOfTaggedCalls,
          "Number of tagged dynamic memory allocation function calls.");

namespace {

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
public:
  using PoisonedStructElement = std::pair<const StructType *, unsigned>;

private:
  Function *AbortF;
  Function *FuzzallocMallocF;
  Function *FuzzallocCallocF;
  Function *FuzzallocReallocF;

  FuzzallocWhitelist Whitelist;

  IntegerType *TagTy;
  IntegerType *SizeTTy;

  std::map<PoisonedStructElement, const Function *> PoisonedStructs;

  ConstantInt *generateTag() const;

  FunctionType *translateTaggedFunctionType(const FunctionType *) const;
  Function *translateTaggedFunction(const Function *) const;
  GlobalVariable *translateTaggedGlobalVariable(GlobalVariable *) const;

  Value *tagUser(User *, Function *, const TargetLibraryInfo *);
  CallInst *tagCall(CallInst *, Value *) const;
  CallInst *tagPossibleIndirectCall(CallInst *) const;
  Function *tagFunction(Function *, const TargetLibraryInfo *);
  GlobalVariable *tagGlobalVariable(GlobalVariable *);
  GlobalAlias *tagGlobalAlias(GlobalAlias *);

public:
  static char ID;
  TagDynamicAllocs() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

static const char *const AbortFuncName = "abort";
static const char *const FuzzallocMallocFuncName = "__tagged_malloc";
static const char *const FuzzallocCallocFuncName = "__tagged_calloc";
static const char *const FuzzallocReallocFuncName = "__tagged_realloc";

char TagDynamicAllocs::ID = 0;

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

static std::pair<StructType *, int64_t>
getTBAAStructTypeWithOffset(const Instruction *I) {
  // Retreive the TBAA metadata
  MemoryLocation ML = MemoryLocation::get(I);
  AAMDNodes AATags = ML.AATags;
  const MDNode *TBAA = AATags.TBAA;
  assert(TBAA && "TBAA must be enabled");

  // Pull apart the access tag
  const MDNode *BaseNode = dyn_cast<MDNode>(TBAA->getOperand(0));
  const ConstantInt *Offset =
      mdconst::dyn_extract<ConstantInt>(TBAA->getOperand(2));

  // TBAA struct type descriptors are represented as MDNodes with an odd number
  // of operands. Retrieve the struct based on the string in the struct type
  // descriptor (the first operand)
  if (BaseNode->getNumOperands() % 2 == 1) {
    const MDString *StructTyName = dyn_cast<MDString>(BaseNode->getOperand(0));
    StructType *StructTy = I->getModule()->getTypeByName(
        "struct." + StructTyName->getString().str());
    assert(StructTy);

    return {StructTy, Offset->getSExtValue()};
  } else {
    assert(false && "Non-struct access tag");
  }
}

static TagDynamicAllocs::PoisonedStructElement
poisonStructElement(StructType *StructTy, int64_t ByteOffset,
                    const DataLayout &DL) {
  const StructLayout *SL = DL.getStructLayout(StructTy);
  unsigned StructIdx = SL->getElementContainingOffset(ByteOffset);
  Type *ElemTy = StructTy->getElementType(StructIdx);

  // Handle nested structs. The recursion will eventually bottom out at some
  // primitive type (ideally, a function pointer).
  //
  // The idea is that the byte offset (calculated via
  // GetPointerBaseWithConstantOffset) may point to some inner struct. If this
  // is the case, then we want to poison the element in the inner struct.
  if (auto *ElemStructTy = dyn_cast<StructType>(ElemTy)) {
    if (!ElemStructTy->isOpaque()) {
      return poisonStructElement(
          ElemStructTy, ByteOffset - SL->getElementOffset(StructIdx), DL);
    }
  }

  // The poisoned element must be a function pointer
  assert(StructTy->getElementType(StructIdx)->isPointerTy());

  return {StructTy, StructIdx};
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

/// Generate a random tag
ConstantInt *TagDynamicAllocs::generateTag() const {
  return ConstantInt::get(this->TagTy, RAND(INST_TAG_START, TAG_MAX));
}

/// Translates a function type to its tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// to the given function type.
FunctionType *TagDynamicAllocs::translateTaggedFunctionType(
    const FunctionType *OrigFTy) const {
  SmallVector<Type *, 4> TaggedFParams = {this->TagTy};
  TaggedFParams.insert(TaggedFParams.end(), OrigFTy->param_begin(),
                       OrigFTy->param_end());

  return FunctionType::get(OrigFTy->getReturnType(), TaggedFParams,
                           OrigFTy->isVarArg());
}

/// Translates a function to its tagged version.
///
/// This inserts a tag (i.e., the call site identifier) as the first argument
/// and prepends the function name with "__tagged_". The returned function also
/// has metadata set denoting that it is a tagged function.
Function *
TagDynamicAllocs::translateTaggedFunction(const Function *OrigF) const {
  FunctionType *NewFTy = translateTaggedFunctionType(OrigF->getFunctionType());
  Twine NewFName = "__tagged_" + OrigF->getName();

  Module *M = const_cast<Module *>(OrigF->getParent());
  auto *NewC = M->getOrInsertFunction(NewFName.str(), NewFTy);

  assert(isa<Function>(NewC) && "Translated tagged function not a function");
  auto *NewF = cast<Function>(NewC);

  NewF->setMetadata(M->getMDKindID("fuzzalloc.tagged_function"),
                    MDNode::get(M->getContext(), None));
  return NewF;
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

/// Translate users of a dynamic memory allocation function so that they use the
/// tagged version instead. Also poisons structs that are assigned dynamic
/// memory allocation functions so that we can rewrite (indirect) calls to these
/// struct elements later
Value *TagDynamicAllocs::tagUser(User *U, Function *F,
                                 const TargetLibraryInfo *TLI) {
  LLVM_DEBUG(dbgs() << "replacing user " << *U << " of tagged function "
                    << F->getName() << '\n');

  if (auto *Call = dyn_cast<CallInst>(U)) {
    // The result of a dynamic memory allocation function call is typically
    // cast. Strip this cast to determine the actual function being called
    auto *CalledValue = Call->getCalledValue()->stripPointerCasts();

    // Work out which tagged function we need to replace the existing
    // function with
    Function *NewF = nullptr;

    if (isMallocLikeFn(Call, TLI)) {
      NewF = this->FuzzallocMallocF;
    } else if (isCallocLikeFn(Call, TLI)) {
      NewF = this->FuzzallocCallocF;
    } else if (isReallocLikeFn(Call, TLI)) {
      NewF = this->FuzzallocReallocF;
    } else if (auto *CalledFunc = dyn_cast<Function>(CalledValue)) {
      if (this->Whitelist.isIn(*CalledFunc)) {
        NewF = translateTaggedFunction(CalledFunc);
      }
    }

    // Replace the original dynamic memory allocation function call
    if (NewF) {
      return tagCall(Call, NewF);
    } else {
      // This should never happen
      assert(false);
    }
  } else if (auto *Store = dyn_cast<StoreInst>(U)) {
    const Module *M = F->getParent();
    const DataLayout &DL = M->getDataLayout();

    // TODO
    assert(!isa<GlobalVariable>(Store->getPointerOperand()) &&
           "Not implemented");

    // Determine the struct type and the index that we are storing the dynamic
    // allocation function to from TBAA metadata. "Poison" the struct and offset
    // so that we can tag it later
    auto StructTyWithOffset = getTBAAStructTypeWithOffset(Store);
    auto PoisonedStructElem = poisonStructElement(
        StructTyWithOffset.first, StructTyWithOffset.second, DL);
    this->PoisonedStructs.emplace(PoisonedStructElem, F);

    // TODO do something more sensible than forcing a runtime abort. This
    // *should* only kick in if the address of the poisoned struct element is
    // taken
    Store->replaceUsesOfWith(
        F, CastInst::CreatePointerCast(this->AbortF, F->getType(), "", Store));

    return Store;
  } else {
    // TODO handle other users
    assert(false && "Unsupported user");
  }
}

/// Replace a function call (`OrigCall`) with a call to `NewCallee` that is
/// tagged with an allocation site identifier.
///
/// The caller must update the users of the original function call to use the
/// tagged version.
CallInst *TagDynamicAllocs::tagCall(CallInst *OrigCall,
                                    Value *NewCallee) const {
  LLVM_DEBUG(dbgs() << "tagging function call " << *OrigCall << " (in function "
                    << OrigCall->getFunction()->getName() << ")\n");

  // The tag value depends where the function call is occuring. If the tagged
  // function is being called from within another tagged function, just pass
  // the first argument (which is guaranteed to be the tag) straight through.
  // Otherwise, generate a new tag. This is determined by reading the metadata
  // of the function
  auto *ParentF = OrigCall->getFunction();
  Value *Tag = ParentF->hasMetadata("fuzzalloc.tagged_function")
                   ? ParentF->arg_begin()
                   : static_cast<Value *>(generateTag());

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

    // XXX assume that function calls to dynamic memory allocation functions can
    // only be bitcast
    auto *BitCast = cast<BitCastInst>(ConstExpr->getAsInstruction());

    assert(isa<FunctionType>(BitCast->getDestTy()->getPointerElementType()) &&
           "Must be a function call bitcast");
    Type *OrigBitCastTy = BitCast->getDestTy()->getPointerElementType();

    // Add the tag (i.e., the call site identifier) as the first argument to
    // the cast function type
    Type *NewBitCastTy =
        translateTaggedFunctionType(cast<FunctionType>(OrigBitCastTy));

    // The callee is a cast version of the tagged function
    CastNewCallee = IRB.CreateBitCast(NewCallee, NewBitCastTy->getPointerTo());

    // getAsInstruction leaves the instruction floating around and unattached to
    // anything, so we must manually delete it
    BitCast->deleteValue();
  } else {
    // The function call result was not cast, so there is no need to do
    // anything to the callee
    CastNewCallee = NewCallee;
  }

  // Create the call to the callee
  auto TaggedCall = IRB.CreateCall(
      CastNewCallee, FuzzallocArgs,
      OrigCall->hasName() ? "__tagged_" + OrigCall->getName() : "");
  TaggedCall->setMetadata(
      TaggedCall->getModule()->getMDKindID("fuzzalloc.tagged_alloc"),
      MDNode::get(IRB.getContext(), None));
  NumOfTaggedCalls++;

  // Replace the users of the original call
  OrigCall->replaceAllUsesWith(TaggedCall);
  OrigCall->eraseFromParent();

  return TaggedCall;
}

/// Possibly replace an indirect function call (`OrigCall`) with a call to a
/// tagged version of the function.
///
/// When will the function call be replaced? Only if the function being called
/// is stored within a poisoned struct. That is, a struct where a whitelisted
/// allocation function was stored into. This is determined via TBAA metadata.
///
/// If the call is not replaced, the original function call is returned.
CallInst *TagDynamicAllocs::tagPossibleIndirectCall(CallInst *OrigCall) const {
  LLVM_DEBUG(dbgs() << "(possibly) tagging indirect function call " << *OrigCall
                    << " (in function " << OrigCall->getFunction()->getName()
                    << ")\n");

  const Module *M = OrigCall->getModule();
  const DataLayout &DL = M->getDataLayout();
  auto *CalledValue = OrigCall->getCalledValue();

  // Get the source of the indirect call and retrieve its TBAA struct type
  Value *Obj = GetUnderlyingObject(CalledValue, DL);
  if (!isa<Instruction>(Obj)) {
    return OrigCall;
  }
  auto StructTyWithOffset = getTBAAStructTypeWithOffset(cast<Instruction>(Obj));

  // If the called value did originate from a struct , check if the struct type
  // is poisoned at this offset
  auto PoisonedStructElem = poisonStructElement(StructTyWithOffset.first,
                                                StructTyWithOffset.second, DL);
  auto PoisonedStructIt = this->PoisonedStructs.find(PoisonedStructElem);
  if (PoisonedStructIt == this->PoisonedStructs.end()) {
    return OrigCall;
  }

  // If the struct type is poisoned, retrieve the function that was assigned
  // to this struct element and tag it
  Function *TaggedF = translateTaggedFunction(PoisonedStructIt->second);
  return tagCall(OrigCall, TaggedF);
}

/// Sometimes a program does not call a dynamic memory allocation function
/// directly, but rather via a allocation wrapper function. For these programs,
/// we must tag the calls to the allocation wrapper function (the `OrigF`
/// argument), rather than the underlying \p malloc / \p calloc / \p realloc
/// call.
///
/// This means that the call site identifier is now associated with the call to
/// the allocation wrapper function, rather than the underlying \p malloc /
/// \p calloc / \p realloc call. When \p malloc / \p calloc / \p realloc is
/// eventually (if at all) called by the allocation wrapper function, the
/// already-generated tag is passed through to the appropriate fuzzalloc
/// function.
Function *TagDynamicAllocs::tagFunction(Function *OrigF,
                                        const TargetLibraryInfo *TLI) {
  LLVM_DEBUG(dbgs() << "tagging function " << OrigF->getName() << '\n');

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

    // Update the contents of the function (i.e., the instructions) when we
    // update the users of the dynamic memory allocation function
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
  LLVM_DEBUG(dbgs() << "tagging global variable " << *OrigGV << '\n');

  GlobalVariable *TaggedGV = translateTaggedGlobalVariable(OrigGV);
  Type *TaggedGVTy = TaggedGV->getValueType();

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
      // Cache users
      SmallVector<User *, 8> LoadUsers(Load->user_begin(), Load->user_end());

      // Load the global variable containing the tagged function
      auto *NewLoad = new LoadInst(
          TaggedGV, Load->hasName() ? "__tagged_" + Load->getName() : "",
          Load->isVolatile(), Load->getAlignment(), Load->getOrdering(),
          Load->getSyncScopeID(), Load);

      for (auto *LU : LoadUsers) {
        if (auto *Call = dyn_cast<CallInst>(LU)) {
          // Replace a call to the function stored in the original global
          // variable with a call to the tagged version
          tagCall(Call, NewLoad);
        } else if (auto *PHI = dyn_cast<PHINode>(LU)) {
          // Replace the loaded global variable with the tagged version
          PHI->replaceUsesOfWith(Load, NewLoad);

          // We can replace the PHI node once all of the PHI node values are of
          // the same type as the tagged global variable
          if (std::all_of(PHI->value_op_begin(), PHI->value_op_end(),
                          [TaggedGVTy](const Value *V) {
                            return V->getType() == TaggedGVTy;
                          })) {
            // Replace the PHI node with an equivalent node of the correct
            // type (i.e., so that it matches the type of the tagged global
            // variable)
            auto *NewPHI = PHINode::Create(
                TaggedGVTy, PHI->getNumIncomingValues(),
                PHI->hasName() ? "__tagged_" + PHI->getName() : "", PHI);
            for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
              NewPHI->addIncoming(PHI->getIncomingValue(i),
                                  PHI->getIncomingBlock(i));
            }

            // Cannot use `replaceAllUsesWith` because the PHI nodes have
            // different types
            for (auto &U : PHI->uses()) {
              U.set(NewPHI);
            }

            // Nothing uses the PHI node now. Delete it
            PHI->eraseFromParent();

            for (auto *PU : NewPHI->users()) {
              // TODO only deal with call instructions for now
              assert(isa<CallInst>(PU));
              auto *Call = cast<CallInst>(PU);

              // Replace a call to the function stored in the original global
              // variable with a call to the tagged version
              tagCall(Call, NewPHI);
            }
          }
        } else {
          // TODO handle other users
          assert(false && "Unsupported user");
        }
      }

      Load->eraseFromParent();
    } else {
      // TODO handle other users
      assert(false && "Unsupported global variable user");
    }
  }

  return TaggedGV;
}

/// A dynamic memory allocation function could be assigned to a global alias.
/// If so, the global alias must be updated to point to a tagged version of the
/// dynamic memory allocation function.
GlobalAlias *TagDynamicAllocs::tagGlobalAlias(GlobalAlias *OrigGA) {
  LLVM_DEBUG(dbgs() << "tagging global alias " << *OrigGA << '\n');

  Constant *OrigAliasee = OrigGA->getAliasee();
  Constant *NewAliasee = nullptr;

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
  assert(OrigGA->getNumUses() == 0 && "Not supported");

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

  LLVMContext &C = M.getContext();
  PointerType *Int8PtrTy = Type::getInt8PtrTy(C);
  Type *VoidTy = Type::getVoidTy(C);

  this->AbortF =
      checkFuzzallocFunc(M.getOrInsertFunction(AbortFuncName, VoidTy));
  this->AbortF->setDoesNotReturn();
  this->AbortF->setDoesNotThrow();

  // Create the tagged memory allocation functions. These functions take the
  // take the same arguments as the original dynamic memory allocation
  // function, except that the first argument is a tag that identifies the
  // allocation site
  this->FuzzallocMallocF = checkFuzzallocFunc(M.getOrInsertFunction(
      FuzzallocMallocFuncName, Int8PtrTy, this->TagTy, this->SizeTTy));
  this->FuzzallocCallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocCallocFuncName, Int8PtrTy, this->TagTy,
                            this->SizeTTy, this->SizeTTy));
  this->FuzzallocReallocF = checkFuzzallocFunc(
      M.getOrInsertFunction(FuzzallocReallocFuncName, Int8PtrTy, this->TagTy,
                            Int8PtrTy, this->SizeTTy));

  SmallVector<Function *, 8> FuncsToDelete = {M.getFunction("malloc"),
                                              M.getFunction("calloc"),
                                              M.getFunction("realloc")};

  for (auto &F : M.functions()) {
    if (this->Whitelist.isIn(F)) {
      tagFunction(&F, TLI);
      FuncsToDelete.push_back(&F);
    }
  }

  for (auto &F : FuncsToDelete) {
    if (!F) {
      continue;
    }

    // Cache users
    SmallVector<User *, 8> Users(F->user_begin(), F->user_end());

    for (auto *U : Users) {
      // This will also poison structs (see below)
      tagUser(U, F, TLI);
    }
  }

  // Handle poisoned structs. These are structs that have a memory allocation
  // function stored to one of their elements and are thus called indirectly
  for (auto &F : M.functions()) {
    for (auto *IndirectCall : findIndirectCallSites(F)) {
      assert(isa<CallInst>(IndirectCall));
      tagPossibleIndirectCall(cast<CallInst>(IndirectCall));
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
    assert(GA->getNumUses() == 0 && "Global alias still has uses");
    GA->eraseFromParent();
  }

  for (auto *GV : GVsToDelete) {
    assert(GV->getNumUses() == 0 && "Global variable still has uses");
    GV->eraseFromParent();
  }

  for (auto *F : FuncsToDelete) {
    if (!F) {
      continue;
    }

    assert(F->getNumUses() == 0 && "Function still has uses");
    F->eraseFromParent();
  }

  // Finished!

  OKF("[%s] %u %s - %s", M.getName().str().c_str(), NumOfTaggedCalls.getValue(),
      NumOfTaggedCalls.getName(), NumOfTaggedCalls.getDesc());

  return true;
}

static RegisterPass<TagDynamicAllocs>
    X("fuzzalloc-tag-dyn-allocs",
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
