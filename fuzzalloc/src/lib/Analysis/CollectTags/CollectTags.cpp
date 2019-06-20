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

#include "llvm/ADT/Statistic.h"
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

#include "Common.h"
#include "debug.h" // from afl

using namespace llvm;

#define DEBUG_TYPE "fuzzalloc-collect-tags"

static cl::opt<std::string>
    ClLogPath("fuzzalloc-tag-log",
              cl::desc("Path to log file containing values to tag"),
              cl::Required);

static cl::opt<std::string>
    ClWhitelist("fuzzalloc-whitelist",
                cl::desc("Path to memory allocation whitelist file"));

STATISTIC(NumOfFunctions, "Number of functions to tag.");
STATISTIC(NumOfGlobalVariables, "Number of global variables to tag.");
STATISTIC(NumOfGlobalAliases, "Number of global aliases to tag.");
STATISTIC(NumOfStructOffsets, "Number of struct offsets to tag.");

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

  SmallPtrSet<const Function *, 8> FunctionsToTag;
  SmallPtrSet<const GlobalVariable *, 8> GlobalVariablesToTag;
  SmallPtrSet<const GlobalAlias *, 8> GlobalAliasesToTag;
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
      NumOfGlobalVariables++;
    } else {
      //  TODO check that the store is to a struct

      // Determine the struct type and the index that we are storing the dynamic
      // allocation function to from TBAA metadata. Calculate the underlying
      // struct and offset so that we can tag it later
      auto StructTyWithOffset = getStructOffsetFromTBAA(Store);
      assert(StructTyWithOffset.hasValue());
      auto StructOff = getStructOffset(StructTyWithOffset->first,
                                       StructTyWithOffset->second, DL);
      this->StructOffsetsToTag.emplace(StructOff, F);
      NumOfStructOffsets++;
    }
  } else if (auto *GV = dyn_cast<GlobalVariable>(U)) {
    // Global variable user
    this->GlobalVariablesToTag.insert(GV);
    NumOfGlobalVariables++;
  } else if (auto *GA = dyn_cast<GlobalAlias>(U)) {
    // Global alias user
    this->GlobalAliasesToTag.insert(GA);
    NumOfGlobalAliases++;
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
    Stream << "unable to open fuzzalloc tag log at " << ClLogPath << ": "
           << EC.message();
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

    Output << StructOffsetLogPrefix << LogSeparator << StructTy->getName()
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

  // Collect all the values to tag

  if (const auto *MallocF = M.getFunction("malloc")) {
    this->FunctionsToTag.insert(MallocF);
  }
  if (const auto *CallocF = M.getFunction("calloc")) {
    this->FunctionsToTag.insert(CallocF);
  }
  if (const auto *ReallocF = M.getFunction("realloc")) {
    this->FunctionsToTag.insert(ReallocF);
  }

  for (const auto &F : M.functions()) {
    if (this->Whitelist.isIn(F)) {
      this->FunctionsToTag.insert(&F);
      NumOfFunctions++;
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

  // Save the collected values
  saveTaggedValues(M);

  if (NumOfFunctions > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(), NumOfFunctions.getValue(),
        NumOfFunctions.getName(), NumOfFunctions.getDesc());
  }
  if (NumOfGlobalVariables > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfGlobalVariables.getValue(), NumOfGlobalVariables.getName(),
        NumOfGlobalVariables.getDesc());
  }
  if (NumOfGlobalAliases > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfGlobalAliases.getValue(), NumOfGlobalAliases.getName(),
        NumOfGlobalAliases.getDesc());
  }
  if (NumOfStructOffsets > 0) {
    OKF("[%s] %u %s - %s", M.getName().str().c_str(),
        NumOfStructOffsets.getValue(), NumOfStructOffsets.getName(),
        NumOfStructOffsets.getDesc());
  }

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
