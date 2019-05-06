//===-- TagDynamicAllocss.h - Tag dynamic memory allocs with unique ID ----===//
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

#ifndef TAG_DYNAMIC_ALLOCS_H
#define TAG_DYNAMIC_ALLOCS_H

#include <map>
#include <set>

#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Pass.h"
#include "llvm/Support/SpecialCaseList.h"

namespace llvm {
class CallInst;
class ConstantInt;
class FunctionType;
class GlobalAlias;
class IntegerType;
class TargetLibraryInfo;
class Value;
} // end namespace llvm

/// Whitelist of dynamic memory allocation wrapper functions and global
/// variables
class FuzzallocWhitelist {
private:
  std::unique_ptr<llvm::SpecialCaseList> SCL;

public:
  FuzzallocWhitelist() = default;

  FuzzallocWhitelist(std::unique_ptr<llvm::SpecialCaseList> List)
      : SCL(std::move(List)){};

  bool isIn(const llvm::Function &F) const {
    return SCL && SCL->inSection("fuzzalloc", "fun", F.getName());
  }

  bool isIn(const llvm::GlobalVariable &GV) const {
    return SCL && SCL->inSection("fuzzalloc", "gv", GV.getName());
  }
};

/// TagDynamicAllocs: Tag dynamic memory allocation function calls (\p malloc,
/// \p calloc and \p realloc) with a randomly-generated identifier (to identify
/// their call site) and call the fuzzalloc function instead
class TagDynamicAllocs : public llvm::ModulePass {
  // Maps function calls to the tagged function that should be called instead^
  using TaggedFunctionMap = std::map<llvm::CallInst *, llvm::Function *>;

private:
  std::set<const llvm::Value *> TaggedAllocs;

  llvm::Function *FuzzallocMallocF;
  llvm::Function *FuzzallocCallocF;
  llvm::Function *FuzzallocReallocF;

  FuzzallocWhitelist Whitelist;

  llvm::IntegerType *TagTy;
  llvm::IntegerType *SizeTTy;

  llvm::ConstantInt *generateTag() const;

  llvm::FunctionType *translateTaggedFunctionType(llvm::FunctionType *) const;
  llvm::Function *translateTaggedFunction(llvm::Function *) const;
  llvm::GlobalVariable *
  translateTaggedGlobalVariable(llvm::GlobalVariable *) const;

  TaggedFunctionMap getDynAllocCalls(llvm::Function *,
                                     const llvm::TargetLibraryInfo *);

  llvm::CallInst *tagCall(llvm::CallInst *, llvm::Value *) const;
  llvm::Function *tagFunction(llvm::Function *,
                              const llvm::TargetLibraryInfo *);
  llvm::GlobalVariable *tagGlobalVariable(llvm::GlobalVariable *);
  llvm::GlobalAlias *tagGlobalAlias(llvm::GlobalAlias *);

public:
  static char ID;
  TagDynamicAllocs() : ModulePass(ID) {}

  const std::set<const llvm::Value *> &getTaggedAllocs() const {
    return this->TaggedAllocs;
  }

  void getAnalysisUsage(llvm::AnalysisUsage &) const override;
  bool doInitialization(llvm::Module &) override;
  bool runOnModule(llvm::Module &) override;
};

#endif // TAG_DYNAMIC_ALLOCS_H
