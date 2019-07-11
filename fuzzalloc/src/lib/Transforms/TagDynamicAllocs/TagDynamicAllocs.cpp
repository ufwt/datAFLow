//===-- TagDynamicAllocs.cpp - Tag dynamic memory allocs with unique ID ---===//
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
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "Common.h"
#include "debug.h"     // from AFL
#include "fuzzalloc.h" // from fuzzalloc

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-tag-dyn-allocs"

// Adapted from http://c-faq.com/lib/randrange.html
#define RAND(x, y) ((tag_t)(x + random() / (RAND_MAX / (y - x + 1) + 1)))

static cl::opt<std::string>
    ClLogPath("fuzzalloc-tag-log",
              cl::desc("Path to log file containing values to tag"));

STATISTIC(NumOfTaggedDirectCalls, "Number of tagged direct function calls.");
STATISTIC(NumOfTaggedIndirectCalls,
          "Number of tagged indirect function calls.");
STATISTIC(NumOfTaggedFunctions, "Number of tagged functions.");
STATISTIC(NumOfTaggedGlobalVariables, "Number of tagged global variables.");
STATISTIC(NumOfTaggedGlobalAliases, "Number of tagged global aliases.");

namespace {

/// TagDynamicAllocs: Tag dynamic memory allocation function calls (\p malloc,
/// \p calloc and \p realloc) with a randomly-generated identifier (to identify
/// their call site) and call the fuzzalloc function instead
class TagDynamicAllocs : public ModulePass {
private:
  using FuncTypeString = std::pair<std::string, std::string>;

  Module *Mod;
  Function *AbortF;
  Function *FuzzallocMallocF;
  Function *FuzzallocCallocF;
  Function *FuzzallocReallocF;

  IntegerType *TagTy;
  IntegerType *SizeTTy;

  SmallPtrSet<Function *, 8> FunctionsToTag;
  SmallPtrSet<GlobalVariable *, 8> GlobalVariablesToTag;
  SmallPtrSet<GlobalAlias *, 8> GlobalAliasesToTag;
  std::map<StructOffset, FuncTypeString> StructOffsetsToTag;
  SmallPtrSet<Argument *, 8> FunctionArgsToTag;

  Constant *castAbort(Type *) const;

  ConstantInt *generateTag() const;
  void getTagSites();

  bool isTaggableFunction(const Function *) const;
  bool isCustomAllocationFunction(const Function *) const;

  FunctionType *translateTaggedFunctionType(const FunctionType *) const;
  Function *translateTaggedFunction(const Function *) const;
  GlobalVariable *translateTaggedGlobalVariable(GlobalVariable *) const;

  void tagUser(User *, Function *, const TargetLibraryInfo *);
  Instruction *tagCallSite(const CallSite &, Value *) const;
  Instruction *tagPossibleIndirectCallSite(const CallSite &);
  Function *tagFunction(Function *);
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
  raw_string_ostream OS(Err);
  OS << "fuzzalloc function redefined: " << *FuncOrBitcast;
  OS.flush();
  report_fatal_error(Err);
}

static bool isReallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                            bool LookThroughBitCast = false) {
  return isAllocationFn(V, TLI, LookThroughBitCast) &&
         !isAllocLikeFn(V, TLI, LookThroughBitCast);
}

Constant *TagDynamicAllocs::castAbort(Type *Ty) const {
  return ConstantExpr::getBitCast(this->AbortF, Ty);
}

/// Generate a random tag
ConstantInt *TagDynamicAllocs::generateTag() const {
  return ConstantInt::get(this->TagTy, RAND(INST_TAG_START, TAG_MAX));
}

void TagDynamicAllocs::getTagSites() {
  if (ClLogPath.empty()) {
    return;
  }

  auto InputOrErr = MemoryBuffer::getFile(ClLogPath);
  std::error_code EC = InputOrErr.getError();
  if (EC) {
    std::string Err;
    raw_string_ostream OS(Err);
    OS << "Unable to open fuzzalloc tag log at " << ClLogPath << ": "
       << EC.message();
    OS.flush();
    report_fatal_error(Err);
  }

  SmallVector<StringRef, 16> Lines;
  StringRef InputData = InputOrErr.get()->getBuffer();
  InputData.split(Lines, '\n', /* MaxSplit */ -1, /* KeepEmpty */ false);

  for (auto Line : Lines) {
    if (Line.startswith(FunctionLogPrefix + LogSeparator)) {
      // Parse function
      SmallVector<StringRef, 3> FString;
      Line.split(FString, LogSeparator);

      auto *F = this->Mod->getFunction(FString[1]);
      if (!F) {
        continue;
      }

      // XXX Ignore the type (for now)

      this->FunctionsToTag.insert(F);
    } else if (Line.startswith(GlobalVariableLogPrefix + LogSeparator)) {
      // Parse global variable
      SmallVector<StringRef, 2> GVString;
      Line.split(GVString, LogSeparator);

      auto *GV = this->Mod->getGlobalVariable(GVString[1]);
      if (!GV) {
        continue;
      }

      this->GlobalVariablesToTag.insert(GV);
    } else if (Line.startswith(GlobalAliasLogPrefix + LogSeparator)) {
      // Parse global alias
      SmallVector<StringRef, 2> GAString;
      Line.split(GAString, LogSeparator);

      auto *GA = this->Mod->getNamedAlias(GAString[1]);
      if (!GA) {
        continue;
      }

      this->GlobalAliasesToTag.insert(GA);
    } else if (Line.startswith(StructOffsetLogPrefix + LogSeparator)) {
      // Parse struct offset
      SmallVector<StringRef, 6> SOString;
      Line.split(SOString, LogSeparator);

      auto *StructTy = this->Mod->getTypeByName(SOString[1]);
      if (!StructTy) {
        continue;
      }

      unsigned Offset;
      if (SOString[2].getAsInteger(10, Offset)) {
        continue;
      }

      // Record the struct function (and type) as a string so that we can later
      // use getOrInsertFunction when we encounter an indirect call
      this->StructOffsetsToTag.emplace(
          std::make_pair(StructTy, Offset),
          std::make_pair(/* Function name */ SOString[3],
                         /* Function type */ SOString[4]));
    } else if (Line.startswith(FunctionArgLogPrefix + LogSeparator)) {
      // Parse function argument
      SmallVector<StringRef, 3> FAString;
      Line.split(FAString, LogSeparator);

      auto *F = this->Mod->getFunction(FAString[1]);
      if (!F) {
        continue;
      }

      unsigned ArgIdx;
      if (FAString[2].getAsInteger(10, ArgIdx)) {
        continue;
      }

      this->FunctionArgsToTag.insert(F->arg_begin() + ArgIdx);
    }
  }
}

bool TagDynamicAllocs::isTaggableFunction(const Function *F) const {
  StringRef Name = F->getName();

  return Name == "malloc" || Name == "calloc" || Name == "realloc" ||
         this->FunctionsToTag.count(F) > 0;
}

bool TagDynamicAllocs::isCustomAllocationFunction(const Function *F) const {
  StringRef Name = F->getName();

  return Name != "malloc" && Name != "calloc" && Name != "realloc" &&
         this->FunctionsToTag.count(F) > 0;
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
/// and prepends the function name with "__tagged_".
Function *
TagDynamicAllocs::translateTaggedFunction(const Function *OrigF) const {
  FunctionType *NewFTy = translateTaggedFunctionType(OrigF->getFunctionType());
  Twine NewFName = "__tagged_" + OrigF->getName();

  Module *M = const_cast<Module *>(OrigF->getParent());
  auto *NewC = M->getOrInsertFunction(NewFName.str(), NewFTy);

  assert(isa<Function>(NewC) && "Translated tagged function not a function");
  auto *NewF = cast<Function>(NewC);

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
/// tagged version instead
void TagDynamicAllocs::tagUser(User *U, Function *F,
                               const TargetLibraryInfo *TLI) {
  LLVM_DEBUG(dbgs() << "replacing user " << *U << " of tagged function "
                    << F->getName() << '\n');

  if (isa<CallInst>(U) || isa<InvokeInst>(U)) {
    // The result of a dynamic memory allocation function call is typically
    // cast. Strip this cast to determine the actual function being called
    CallSite CS(cast<Instruction>(U));
    auto *CalledValue = CS.getCalledValue()->stripPointerCasts();

    // Work out which tagged function we need to replace the existing
    // function with
    Function *NewF = nullptr;

    if (isMallocLikeFn(U, TLI)) {
      NewF = this->FuzzallocMallocF;
    } else if (isCallocLikeFn(U, TLI)) {
      NewF = this->FuzzallocCallocF;
    } else if (isReallocLikeFn(U, TLI)) {
      NewF = this->FuzzallocReallocF;
    } else if (auto *CalledFunc = dyn_cast<Function>(CalledValue)) {
      if (this->FunctionsToTag.count(CalledFunc) > 0) {
        // The user is the called function itself. Tag the function call
        NewF = translateTaggedFunction(CalledFunc);
      } else {
        // The user of a dynamic allocation function must be an argument to the
        // function call
        //
        // There isn't much we can do in this case (because we do not perform an
        // interprocedural analysis) except to replace the function pointer with
        // a pointer to the abort function and handle this at runtime

        WARNF("[%s] Replacing %s function argument with an abort",
              this->Mod->getName().str().c_str(),
              CalledFunc->getName().str().c_str());
        U->replaceUsesOfWith(F, castAbort(F->getType()));
      }
    }

    // Replace the original dynamic memory allocation function call
    if (NewF) {
      tagCallSite(CS, NewF);
    }
  } else if (auto *Store = dyn_cast<StoreInst>(U)) {
    if (auto *GV = dyn_cast<GlobalVariable>(Store->getPointerOperand())) {
      // Tag stores to global variables
      tagGlobalVariable(GV);
    } else {
      // TODO check that this store is to a struct in StructOffsetsToTag

      // TODO do something more sensible than forcing a runtime abort. This
      // *should* only kick in if the address of the struct element containing
      // the memory allocation function is taken
      std::string PtrOpStr;
      raw_string_ostream OS(PtrOpStr);
      OS << *Store->getPointerOperand();

      WARNF("[%s] Replacing store to %s with an abort",
            this->Mod->getName().str().c_str(), PtrOpStr.c_str());
      Store->replaceUsesOfWith(F, castAbort(F->getType()));
    }
  } else if (auto *GV = dyn_cast<GlobalVariable>(U)) {
    // Tag global variables
    tagGlobalVariable(GV);
  } else if (auto *GA = dyn_cast<GlobalAlias>(U)) {
    // Tag global aliases
    tagGlobalAlias(GA);
  } else if (auto *Const = dyn_cast<Constant>(U)) {
    // Warn on unsupported constant user and replace with an abort. This is
    // treated separately because we cannot call replaceUsesOfWith on a constant
    std::string UserStr;
    raw_string_ostream OS(UserStr);
    OS << *U;

    WARNF("[%s] Replacing unsupported constant user %s with an abort",
          this->Mod->getName().str().c_str(), UserStr.c_str());
    Const->handleOperandChange(F, castAbort(F->getType()));
  } else {
    // Warn on unsupported user and replace with an undef value
    std::string UserStr;
    raw_string_ostream OS(UserStr);
    OS << *U;

    WARNF("[%s] Replacing unsupported user %s with an undef value",
          this->Mod->getName().str().c_str(), UserStr.c_str());
    U->replaceUsesOfWith(F, UndefValue::get(F->getType()));
  }
}

/// Replace a function call site (`CS`) with a call to `NewCallee` that is
/// tagged with an allocation site identifier.
///
/// The caller must update the users of the original function call site to use
/// the tagged version.
Instruction *TagDynamicAllocs::tagCallSite(const CallSite &CS,
                                           Value *NewCallee) const {
  LLVM_DEBUG(dbgs() << "tagging call site " << *CS.getInstruction()
                    << " (in function " << CS->getFunction()->getName()
                    << ")\n");

  // The tag value depends where the function call is occuring. If the tagged
  // function is being called from within another tagged function, just pass
  // the first argument (which is guaranteed to be the tag) straight through.
  // Otherwise, generate a new tag. This is determined by reading the metadata
  // of the function
  auto *ParentF = CS->getFunction();
  Value *Tag = this->FunctionsToTag.count(ParentF) > 0
                   ? translateTaggedFunction(ParentF)->arg_begin()
                   : static_cast<Value *>(generateTag());

  // Copy the original allocation function call's arguments so that the tag is
  // the first argument passed to the tagged function
  SmallVector<Value *, 3> FuzzallocArgs = {Tag};
  FuzzallocArgs.insert(FuzzallocArgs.end(), CS.arg_begin(), CS.arg_end());

  Value *CastNewCallee;
  IRBuilder<> IRB(CS.getInstruction());

  if (auto *BitCast = dyn_cast<BitCastOperator>(CS.getCalledValue())) {
    // Sometimes the result of the original dynamic memory allocation function
    // call is cast to some other pointer type. because this is a function
    // call, the underlying type should still be a function type
    Type *OrigBitCastTy = BitCast->getDestTy()->getPointerElementType();
    assert(isa<FunctionType>(OrigBitCastTy) &&
           "Must be a function call bitcast");

    // Add the tag (i.e., the call site identifier) as the first argument to
    // the cast function type
    Type *NewBitCastTy =
        translateTaggedFunctionType(cast<FunctionType>(OrigBitCastTy));

    // The callee is a cast version of the tagged function
    CastNewCallee = IRB.CreateBitCast(NewCallee, NewBitCastTy->getPointerTo());
  } else {
    // The function call result was not cast, so there is no need to do
    // anything to the callee
    CastNewCallee = NewCallee;
  }

  // Create the call/invoke to the callee/invokee
  Instruction *TaggedCall = nullptr;
  if (CS.isCall()) {
    TaggedCall = IRB.CreateCall(CastNewCallee, FuzzallocArgs);
  } else if (CS.isInvoke()) {
    auto *Invoke = cast<InvokeInst>(CS.getInstruction());
    TaggedCall = IRB.CreateInvoke(CastNewCallee, Invoke->getNormalDest(),
                                  Invoke->getUnwindDest(), FuzzallocArgs);
  }
  TaggedCall->setMetadata(this->Mod->getMDKindID("fuzzalloc.tagged_alloc"),
                          MDNode::get(IRB.getContext(), None));

  if (CS.isIndirectCall()) {
    NumOfTaggedIndirectCalls++;
  } else {
    NumOfTaggedDirectCalls++;
  }

  // Replace the users of the original call
  CS->replaceAllUsesWith(TaggedCall);
  CS->eraseFromParent();

  return TaggedCall;
}

/// Possibly replace an indirect function call site (`CS`) with a call to a
/// tagged version of the function.
///
/// The function call will only be replaced if the function being called is
/// stored within a recorded struct. That is, a struct where a whitelisted
/// allocation function was stored into.
///
/// If the call is not replaced, the original function call is returned.
Instruction *TagDynamicAllocs::tagPossibleIndirectCallSite(const CallSite &CS) {
  LLVM_DEBUG(dbgs() << "(possibly) tagging indirect function call "
                    << *CS.getInstruction() << " (in function "
                    << CS->getFunction()->getName() << ")\n");

  auto *CSInst = CS.getInstruction();
  const DataLayout &DL = this->Mod->getDataLayout();
  auto *CalledValue = CS.getCalledValue();
  auto *CalledValueTy = CS.getFunctionType();

  // Get the source of the indirect call. If the called value didn't come from a
  // load, we can't do anything about it
  Value *Obj = GetUnderlyingObject(CalledValue, DL);
  if (!isa<LoadInst>(Obj)) {
    return CSInst;
  }
  auto *ObjLoad = cast<LoadInst>(Obj);

  int64_t ByteOffset = 0;
  auto *ObjBase =
      GetPointerBaseWithConstantOffset(ObjLoad->getOperand(0), ByteOffset, DL);
  Type *ObjBaseElemTy = ObjBase->getType()->getPointerElementType();

  // TODO check that the load is actually from a struct
  if (!isa<StructType>(ObjBaseElemTy)) {
    return CSInst;
  }

  // If the called value did originate from a struct, check if the struct
  // offset is one we previously recorded (in the collect tags pass)
  auto StructOffset =
      getStructOffset(cast<StructType>(ObjBaseElemTy), ByteOffset, DL);
  if (!StructOffset.hasValue()) {
    return CSInst;
  }

  auto StructOffsetIt = this->StructOffsetsToTag.find(*StructOffset);
  if (StructOffsetIt == this->StructOffsetsToTag.end()) {
    return CSInst;
  }

  // The struct type was recorded. Retrieve the function that was assigned to
  // this struct element and tag it
  StringRef OrigFStr = StructOffsetIt->second.first;

  // Sanity check the function type
  //
  // XXX Comparing strings seems hella dirty...
  std::string OrigCallTyStr;
  raw_string_ostream OS(OrigCallTyStr);
  OS << *CalledValueTy;
  OS.flush();
  assert(OrigCallTyStr == StructOffsetIt->second.second);

  // get-or-insert the function, rather than just getting it. Since the original
  // funtion is being called indirectly (via a struct), it is highly-likely that
  // the original function is not actually defined in this module (otherwise
  // we'd just call it directly)
  //
  // Save the function so that we can delete it later
  Function *OrigF = checkFuzzallocFunc(
      this->Mod->getOrInsertFunction(OrigFStr, CalledValueTy));
  this->FunctionsToTag.insert(OrigF);

  return tagCallSite(CS, translateTaggedFunction(OrigF));
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
Function *TagDynamicAllocs::tagFunction(Function *OrigF) {
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

    NumOfTaggedFunctions++;
  }

  return TaggedF;
}

/// A dynamic memory allocation function could be assigned to a global
/// variable (which is different to a global alias). If so, the global variable
/// must be updated to point to a tagged version of the dynamic memory
/// allocation function.
///
/// Which global variables get tagged is determined by stores of whitelisted
/// functions.
GlobalVariable *TagDynamicAllocs::tagGlobalVariable(GlobalVariable *OrigGV) {
  LLVM_DEBUG(dbgs() << "tagging global variable " << *OrigGV << '\n');

  // Cache users
  SmallVector<User *, 16> Users(OrigGV->user_begin(), OrigGV->user_end());

  // Translate the global variable to get a tagged version. Since it is a globa;
  // variable casting to a pointer type is safe (all globals are pointers)
  GlobalVariable *TaggedGV = translateTaggedGlobalVariable(OrigGV);
  PointerType *TaggedGVTy = cast<PointerType>(TaggedGV->getValueType());

  // Replace the initializer (if it exists) with a tagged version
  if (OrigGV->hasInitializer()) {
    auto *OrigInitializer = OrigGV->getInitializer();

    if (auto *InitializerF = dyn_cast<Function>(OrigInitializer)) {
      // Tag the initializer function
      TaggedGV->setInitializer(translateTaggedFunction(InitializerF));
    } else if (isa<ConstantPointerNull>(OrigInitializer)) {
      // Retype the null pointer initializer
      TaggedGV->setInitializer(ConstantPointerNull::get(TaggedGVTy));
    } else {
      assert(false && "Unsupported global variable initializer");
    }
  }

  // Replace all the users of the global variable
  for (auto *U : Users) {
    if (auto *Load = dyn_cast<LoadInst>(U)) {
      // Cache users
      SmallVector<User *, 8> LoadUsers(Load->user_begin(), Load->user_end());

      // Load the global variable containing the tagged function
      auto *NewLoad = new LoadInst(
          TaggedGV, Load->hasName() ? "__tagged_" + Load->getName() : "",
          Load->isVolatile(), Load->getAlignment(), Load->getOrdering(),
          Load->getSyncScopeID(), Load);

      for (auto *LU : LoadUsers) {
        if (isa<CallInst>(LU) || isa<InvokeInst>(LU)) {
          // Replace a call to the function stored in the original global
          // variable with a call to the tagged version
          tagCallSite(CallSite(LU), NewLoad);
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
              assert(isa<CallInst>(PU) || isa<InvokeInst>(PU));

              // Replace a call to the function stored in the original global
              // variable with a call to the tagged version
              tagCallSite(CallSite(PU), NewPHI);
            }
          }
        } else {
          // TODO handle other users
          assert(false && "Unsupported global variable load user");
        }
      }

      Load->eraseFromParent();
    } else if (auto *Store = dyn_cast<StoreInst>(U)) {
      // The only things that should be written to a tagged global variable are
      // functions that are going to be tagged
      if (auto *F = dyn_cast<Function>(Store->getValueOperand())) {
        assert(isTaggableFunction(F));
        auto *NewStore =
            new StoreInst(translateTaggedFunction(F), TaggedGV,
                          Store->isVolatile(), Store->getAlignment(),
                          Store->getOrdering(), Store->getSyncScopeID(), Store);
        Store->replaceAllUsesWith(NewStore);
        Store->eraseFromParent();
      } else {
        // We cannot determine anything about the value being stored - just
        // replace it with the abort function and hope for the best
        WARNF("[%s] Replacing store to %s with an abort",
              this->Mod->getName().str().c_str(),
              OrigGV->getName().str().c_str());
        Store->replaceUsesOfWith(OrigGV, castAbort(OrigGV->getType()));
      }
    } else if (auto *BitCast = dyn_cast<BitCastOperator>(U)) {
      // Cache users
      SmallVector<User *, 16> BitCastUsers(BitCast->user_begin(),
                                           BitCast->user_end());

      for (auto *BCU : BitCastUsers) {
        assert(isa<Instruction>(BCU));
        auto *NewBitCast = CastInst::CreateBitOrPointerCast(
            TaggedGV, BitCast->getDestTy(), "", cast<Instruction>(BCU));
        BCU->replaceUsesOfWith(BitCast, NewBitCast);
      }
      BitCast->deleteValue();
    } else {
      // TODO handle other users
      assert(false && "Unsupported global variable user");
    }
  }

  NumOfTaggedGlobalVariables++;

  return TaggedGV;
}

/// A dynamic memory allocation function could be assigned to a global alias.
/// If so, the global alias must be updated to point to a tagged version of the
/// dynamic memory allocation function.
GlobalAlias *TagDynamicAllocs::tagGlobalAlias(GlobalAlias *OrigGA) {
  LLVM_DEBUG(dbgs() << "tagging global alias " << *OrigGA << '\n');

  Constant *OrigAliasee = OrigGA->getAliasee();
  Constant *TaggedAliasee = nullptr;

  if (auto *AliaseeF = dyn_cast<Function>(OrigAliasee)) {
    TaggedAliasee = translateTaggedFunction(AliaseeF);
  } else if (auto *AliaseeGV = dyn_cast<GlobalVariable>(OrigAliasee)) {
    TaggedAliasee = translateTaggedGlobalVariable(AliaseeGV);
  } else {
    assert(false &&
           "Global alias aliasee must be a function or global variable");
  }

  auto *TaggedGA = GlobalAlias::create(
      TaggedAliasee->getType()->getPointerElementType(),
      TaggedAliasee->getType()->getPointerAddressSpace(), OrigGA->getLinkage(),
      OrigGA->hasName() ? "__tagged_" + OrigGA->getName() : "", TaggedAliasee,
      OrigGA->getParent());

  // TODO handle users
  assert(OrigGA->getNumUses() == 0 && "Not supported");

  NumOfTaggedGlobalAliases++;

  return TaggedGA;
}

void TagDynamicAllocs::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool TagDynamicAllocs::doInitialization(Module &M) {
  LLVMContext &C = M.getContext();
  const DataLayout &DL = M.getDataLayout();

  this->Mod = &M;
  this->TagTy = Type::getIntNTy(C, NUM_TAG_BITS);
  this->SizeTTy = DL.getIntPtrType(C);

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

  // Figure out what we need to tag
  getTagSites();

  // Tag all the things

  for (auto *F : this->FunctionsToTag) {
    // Only rewrite custom allocation functions (i.e., not malloc, calloc, or
    // realloc)
    if (isCustomAllocationFunction(F)) {
      tagFunction(F);
    }
  }

  for (auto *F : this->FunctionsToTag) {
    // Cache users
    SmallVector<User *, 16> Users(F->user_begin(), F->user_end());

    for (auto *U : Users) {
      tagUser(U, F, TLI);
    }
  }

  for (auto *GV : this->GlobalVariablesToTag) {
    tagGlobalVariable(GV);
  }

  for (auto *GA : this->GlobalAliasesToTag) {
    tagGlobalAlias(GA);
  }

  for (auto &F : M.functions()) {
    for (auto *IndirectCall : findIndirectCallSites(F)) {
      tagPossibleIndirectCallSite(CallSite(IndirectCall));
    }
  }

  // Delete all the things that have been tagged

  for (auto *GA : this->GlobalAliasesToTag) {
    assert(GA->getNumUses() == 0 && "Global alias still has uses");
    GA->eraseFromParent();
  }

  for (auto *GV : this->GlobalVariablesToTag) {
    assert(GV->getNumUses() == 0 && "Global variable still has uses");
    GV->eraseFromParent();
  }

  for (auto *F : this->FunctionsToTag) {
    assert(F->getNumUses() == 0 && "Function still has uses");
    F->eraseFromParent();
  }

  // Finished!

  if (NumOfTaggedDirectCalls > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfTaggedDirectCalls.getValue(), NumOfTaggedDirectCalls.getName(),
        NumOfTaggedDirectCalls.getDesc());
  }
  if (NumOfTaggedIndirectCalls > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfTaggedIndirectCalls.getValue(), NumOfTaggedIndirectCalls.getName(),
        NumOfTaggedIndirectCalls.getDesc());
  }
  if (NumOfTaggedFunctions > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfTaggedFunctions.getValue(), NumOfTaggedFunctions.getName(),
        NumOfTaggedFunctions.getDesc());
  }
  if (NumOfTaggedGlobalVariables > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfTaggedGlobalVariables.getValue(),
        NumOfTaggedGlobalVariables.getName(),
        NumOfTaggedGlobalVariables.getDesc());
  }
  if (NumOfTaggedGlobalAliases > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfTaggedGlobalAliases.getValue(), NumOfTaggedGlobalAliases.getName(),
        NumOfTaggedGlobalAliases.getDesc());
  }

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
