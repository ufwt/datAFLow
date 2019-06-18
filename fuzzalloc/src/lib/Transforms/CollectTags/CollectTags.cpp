//===-- CollectTags.cpp - Collects values to tag in later passes ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass collects values (functions, global variables/alias, and structs)
/// that will require tagging by the \c TagDynamicAllocs pass.
///
//===----------------------------------------------------------------------===//

#include <map>

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "Utils.h"

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-collect-tags"

static cl::opt<std::string>
    ClLogPath("fuzzalloc-tag-log",
              cl::desc("Path to log file for values to tag"), cl::Required);

static cl::opt<std::string>
    ClWhitelist("fuzzalloc-whitelist",
                cl::desc("Path to memory allocation whitelist file"));

namespace {

/// Whitelist of dynamic memory allocation wrapper functions
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

/// Log values that require tagging later on
class CollectTags : public ModulePass {
private:
  FuzzallocWhitelist Whitelist;

  SmallPtrSet<const Function *, 16> FunctionsToTag;
  SmallPtrSet<const GlobalVariable *, 16> GlobalVariablesToTag;
  SmallPtrSet<const GlobalAlias *, 16> GlobalAliasesToTag;
  std::map<StructOffset, const Function *> StructOffsetsToTag;

  void tagUser(const User *, const Function *, const TargetLibraryInfo *);
  void saveTaggedValues(const Module &) const;

public:
  static char ID;
  CollectTags() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &) const override;
  bool doInitialization(Module &) override;
  bool runOnModule(Module &) override;
};

} // anonymous namespace

static const char *const CommentStart = "# ";
static const char *const LogSeparator = ",";
static const char *const FunctionLogPrefix = "fun";
static const char *const GlobalVariableLogPrefix = "gv";
static const char *const GlobalAliasLogPrefix = "ga";
static const char *const StructLogPrefix = "struct";

char CollectTags::ID = 0;

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
static StructOffset getStructOffset(StructType *StructTy, int64_t ByteOffset,
                                    const DataLayout &DL) {
  const StructLayout *SL = DL.getStructLayout(StructTy);
  unsigned StructIdx = SL->getElementContainingOffset(ByteOffset);
  Type *ElemTy = StructTy->getElementType(StructIdx);

  // Handle nested structs. The recursion will eventually bottom out at some
  // primitive type (ideally, a function pointer).
  //
  // The idea is that the byte offset (read from TBAA access metadata) may
  // point to some inner struct. If this is the case, then we want to poison
  // the element in the inner struct so that we can tag calls to it later
  if (auto *ElemStructTy = dyn_cast<StructType>(ElemTy)) {
    if (!ElemStructTy->isOpaque()) {
      return getStructOffset(ElemStructTy,
                             ByteOffset - SL->getElementOffset(StructIdx), DL);
    }
  }

  // The poisoned element must be a function pointer
  assert(StructTy->getElementType(StructIdx)->isPointerTy());

  return {StructTy, StructIdx};
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
  assert(BaseNode->getNumOperands() % 2 == 1 && "Non-struct access tag");

  const MDString *StructTyName = dyn_cast<MDString>(BaseNode->getOperand(0));
  StructType *StructTy = I->getModule()->getTypeByName(
      "struct." + StructTyName->getString().str());
  if (!StructTy) {
    return {nullptr, 0};
  }

  return {StructTy, Offset->getSExtValue()};
}

void CollectTags::tagUser(const User *U, const Function *F,
                          const TargetLibraryInfo *TLI) {
  if (auto *Call = dyn_cast<CallInst>(U)) {
    // Ignore calls for now - we can just tag them directly
  } else if (auto *Store = dyn_cast<StoreInst>(U)) {
    const Module *M = F->getParent();
    const DataLayout &DL = M->getDataLayout();

    if (auto *GV = dyn_cast<GlobalVariable>(Store->getPointerOperand())) {
      // Store to a global variable
      this->GlobalVariablesToTag.insert(GV);
    } else {
      //  TODO check that the store is to a struct

      // Determine the struct type and the index that we are storing the dynamic
      // allocation function to from TBAA metadata. Calculate the underlying
      // struct and offset so that we can tag it later
      auto StructTyWithOffset = getTBAAStructTypeWithOffset(Store);
      assert(StructTyWithOffset.first != nullptr);
      auto StructOff = getStructOffset(StructTyWithOffset.first,
                                       StructTyWithOffset.second, DL);
      this->StructOffsetsToTag.emplace(StructOff, F);
    }
  } else if (auto *GV = dyn_cast<GlobalVariable>(U)) {
    // Global variable user
    this->GlobalVariablesToTag.insert(GV);
  } else if (auto *GA = dyn_cast<GlobalAlias>(U)) {
    // Global alias user
    this->GlobalAliasesToTag.insert(GA);
  } else {
    assert(false && "Unsupported user");
  }
}

void CollectTags::saveTaggedValues(const Module &M) const {
  std::error_code EC;
  raw_fd_ostream Output(ClLogPath, EC,
                        sys::fs::OpenFlags::OF_Text |
                            sys::fs::OpenFlags::OF_Append);
  if (EC) {
    std::string Err;
    raw_string_ostream Stream(Err);
    Stream << "unable to open fuzzalloc tag log at " << ClLogPath;
    report_fatal_error(Err);
  }

  // Add a comment
  Output << CommentStart << M.getName() << '\n';

  // Save functions
  for (auto *F : this->FunctionsToTag) {
    if (!F) {
      continue;
    }

    Output << FunctionLogPrefix << LogSeparator << F->getName() << '\n';
  }

  // Save global variables
  for (auto *GV : this->GlobalVariablesToTag) {
    Output << GlobalVariableLogPrefix << LogSeparator << GV->getName() << '\n';
  }

  // Save global aliases
  for (auto *GA : this->GlobalAliasesToTag) {
    Output << GlobalAliasLogPrefix << LogSeparator << GA->getName() << '\n';
  }

  // Save struct mappings
  for (const auto &StructWithFunc : this->StructOffsetsToTag) {
    auto *StructTy = StructWithFunc.first.first;
    unsigned Offset = StructWithFunc.first.second;
    auto *F = StructWithFunc.second;

    Output << StructLogPrefix << LogSeparator << StructTy->getName()
           << LogSeparator << Offset << LogSeparator << F->getName() << '\n';
  }
}

void CollectTags::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

bool CollectTags::doInitialization(Module &M) {
  this->Whitelist = getWhitelist();

  return false;
}

bool CollectTags::runOnModule(Module &M) {
  const TargetLibraryInfo *TLI =
      &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  this->FunctionsToTag.insert({M.getFunction("malloc"), M.getFunction("calloc"),
                               M.getFunction("realloc")});

  for (const auto &F : M.functions()) {
    if (this->Whitelist.isIn(F)) {
      this->FunctionsToTag.insert(&F);
    }
  }

  for (auto *F : this->FunctionsToTag) {
    if (!F) {
      continue;
    }

    for (auto *U : F->users()) {
      tagUser(U, F, TLI);
    }
  }

  saveTaggedValues(M);

  return false;
}

static RegisterPass<CollectTags> X("fuzzalloc-collect-tags",
                                   "Collect values that will require tagging",
                                   false, false);

static void registerCollectTagsPass(const PassManagerBuilder &,
                                    legacy::PassManagerBase &PM) {
  PM.add(new CollectTags());
}

static RegisterStandardPasses
    RegisterCollectTagsPass(PassManagerBuilder::EP_OptimizerLast,
                            registerCollectTagsPass);

static RegisterStandardPasses
    RegisterCollectTagsPass0(PassManagerBuilder::EP_EnabledOnOptLevel0,
                             registerCollectTagsPass);
