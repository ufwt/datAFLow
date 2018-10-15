//===- EscapeAnalysis.h - Pointer Escape Analysis ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Escape Analysis pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ESCAPEANALYSIS_H
#define LLVM_ANALYSIS_ESCAPEANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#include <unordered_map>
#include <vector>

/// Calculates escape points for the given function.
///
/// This class is responsible for identifying allocations that escape their
/// enclosing function. For each allocation a list is kept containing the
/// instructions that form possible points of escape.
class EscapeInfo {
public:
  EscapeInfo() = default;

  void recalculate(llvm::Function &F, llvm::AliasAnalysis &AA);
  bool escapes(const llvm::Value *V) const;
  void print(llvm::raw_ostream &O) const;

private:
  std::unordered_map<const llvm::Value *,
                     std::vector<const llvm::Instruction *>>
      EscapePoints;
};

/// Legacy analysis pass that exposes the EscapeInfo for a function.
///
/// It runs the analysis for a function on demand. This can be initiated by
/// querying the escape info via EscapeAnalsis::getInfo, which returns a
/// EscapeInfo object.
class EscapeAnalysisPass : public llvm::FunctionPass {
public:
  EscapeAnalysisPass();

  bool runOnFunction(llvm::Function &F) override;
  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
  void print(llvm::raw_ostream &O, const llvm::Module *M) const override;

  EscapeInfo &getEscapeInfo() { return EI; }
  const EscapeInfo &getEscapeInfo() const { return EI; }

  static char ID;

private:
  EscapeInfo EI;
};

/// Analysis pass that exposes the EscapeInfo for a function.
///
/// It runs the analysis for a function on demand. This can be inintiated by
/// querying the escape info via AM.getResult<EscapeAnalysis>, which returns an
/// EscapeInfo object.
class EscapeAnalysis : public llvm::AnalysisInfoMixin<EscapeAnalysis> {
  friend AnalysisInfoMixin<EscapeAnalysis>;

  static llvm::AnalysisKey Key;

public:
  using Result = EscapeInfo;

  EscapeInfo run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

/// Printer pass for the EscapeInfo results.
class EscapeAnalysisPrinterPass
    : public llvm::PassInfoMixin<EscapeAnalysisPrinterPass> {
  llvm::raw_ostream &OS;

public:
  explicit EscapeAnalysisPrinterPass(llvm::raw_ostream &OS);
  llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

#endif
