//===- EscapeAnalysis.cpp - Pointer Escape Analysis ------------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the EscapeAnalysis analysis pass.
//
//===----------------------------------------------------------------------===//

#include "EscapeAnalysis.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>

using namespace llvm;

#define DEBUG_TYPE "escape-analysis"

static bool IsAlloc(llvm::Instruction *I) {
  if (CallInst *CI = dyn_cast<CallInst>(I)) {
    llvm::Function *Callee = CI->getCalledFunction();
    if (Callee == nullptr || !Callee->isDeclaration())
      return false;
    return Callee->getName() == "malloc";
  }

  return isa<AllocaInst>(I);
}

void EscapeInfo::recalculate(llvm::Function &F, AliasAnalysis &AA) {
  for (auto &BB : F) {
    for (auto &BBI : BB) {

      Instruction *I = &BBI;
      if (!IsAlloc(I))
        continue;

      std::vector<Instruction *> Worklist = {I};
      std::unordered_set<const Instruction *> Visited;

      while (!Worklist.empty()) {
        Instruction *CurrentInst = Worklist.back();
        Worklist.pop_back();

        if (Visited.count(CurrentInst))
          continue;
        Visited.insert(CurrentInst);

        for (User *U : CurrentInst->users()) {
          // Add this user to the worklist.
          if (auto Inst = dyn_cast<Instruction>(U))
            Worklist.push_back(Inst);

          // No escape through load instructions, no need to look further.
          if (isa<LoadInst>(U))
            continue;

          // Returns allow the return value to escape. This is mostly important
          // for malloc to alloca promotion.
          if (auto Inst = dyn_cast<ReturnInst>(U)) {
            EscapePoints[I].push_back(Inst);
            continue;
          }

          // Calls potentially allow their parameters to escape.
          if (auto Inst = dyn_cast<CallInst>(U)) {
            EscapePoints[I].push_back(Inst);
            continue;
          }

          // Like calls, invokes potentially allow their parameters to escape.
          if (auto Inst = dyn_cast<InvokeInst>(U)) {
            EscapePoints[I].push_back(Inst);
            continue;
          }

          // The most obvious case: stores. Any store that writes to global
          // memory or to a function argument potentially allows its input to
          // escape.
          if (auto *Inst = dyn_cast<StoreInst>(U)) {
            Value *Ptr = Inst->getPointerOperand();

            bool Escapes = false;

            // Check function arguments
            for (auto &Arg : F.args()) {
              if (!isa<PointerType>(Arg.getType()))
                continue;

              if (auto *V = dyn_cast<Value>(&Arg)) {
                if (!AA.isNoAlias(Ptr, V)) {
                  EscapePoints[I].push_back(Inst);
                  Escapes = true;
                  break;
                }
              }
            }

            // No need to check globals if we already found an escape point.
            if (Escapes)
              continue;

            // Check globals for escape.
            for (auto &Global : F.getParent()->globals()) {
              if (auto *V = dyn_cast<Value>(&Global)) {
                if (!AA.isNoAlias(Ptr, V)) {
                  EscapePoints[I].push_back(Inst);
                  Escapes = true;
                  break;
                }
              }
            }

            // No need to check pointer uses if we already found an escape
            // point.
            if (Escapes)
              continue;

            // Check pointer uses for escape.
            if (auto *PtrInst = dyn_cast<Instruction>(Ptr)) {
                Worklist.push_back(PtrInst);
            }
          }
        }
      }
    }
  }
}

bool EscapeInfo::escapes(const Value *V) const {
  auto it = EscapePoints.find(V);
  if (it == EscapePoints.cend())
    return false;

  return !it->second.empty();
}

void EscapeInfo::print(llvm::raw_ostream &O) const {
  for (auto P : EscapePoints) {
    O << "Value '" << P.first->getName() << "' has " << P.second.size()
      << " possible escape point(s).\n";
  }
}

char EscapeAnalysisPass::ID = 0;

static RegisterPass<EscapeAnalysisPass> EscapeAnalysisPassPass(
        "escape-analysis", "Escape Analysis", true, true);

EscapeAnalysisPass::EscapeAnalysisPass() : llvm::FunctionPass(ID) {}

void EscapeAnalysisPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
}

bool EscapeAnalysisPass::runOnFunction(llvm::Function &F) {
  EI.recalculate(F, getAnalysis<AAResultsWrapperPass>().getAAResults());
  return false;
}

void EscapeAnalysisPass::print(llvm::raw_ostream &O, const Module *M) const {
  EI.print(O);
}

AnalysisKey EscapeAnalysis::Key;

EscapeInfo EscapeAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  EscapeInfo EI;
  EI.recalculate(F, AM.getResult<AAManager>(F));
  return EI;
}

EscapeAnalysisPrinterPass::EscapeAnalysisPrinterPass(raw_ostream &OS)
    : OS(OS) {}

PreservedAnalyses EscapeAnalysisPrinterPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  OS << "Escape Analysis for function: " << F.getName() << "\n";
  AM.getResult<EscapeAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}
