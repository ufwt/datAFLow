//===-- InstrumentDereferences.h - Instrument pointer dereferences --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass instruments pointer dereferences (i.e., \p load instructions) to
/// discover their allocation site.
///
//===----------------------------------------------------------------------===//

#ifndef INSTRUMENT_DEREFERENCES_H
#define INSTRUMENT_DEREFERENCES_H

#include <set>

#include "llvm/Pass.h"

namespace llvm {
class ConstantInt;
class Instruction;
class IntegerType;
class Value;
} // end namespace llvm

class InstrumentDereferences : public llvm::ModulePass {
private:
  std::set<const llvm::Value *> InstrumentedDereferences;

  llvm::IntegerType *Int64Ty;
  llvm::IntegerType *TagTy;

  llvm::ConstantInt *TagShiftSize;
  llvm::ConstantInt *TagMask;

  void doInstrumentDeref(llvm::Instruction *, llvm::Value *, llvm::Function *);

public:
  static char ID;
  InstrumentDereferences() : ModulePass(ID) {}

  const std::set<const llvm::Value *> &getInstrumentedDereferences() const {
    return this->InstrumentedDereferences;
  }

  void getAnalysisUsage(llvm::AnalysisUsage &) const override;
  bool doInitialization(llvm::Module &) override;
  bool runOnModule(llvm::Module &) override;
};

#endif // INSTRUMENT_DEREFERENCES_H
