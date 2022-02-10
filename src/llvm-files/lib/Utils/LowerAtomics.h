//===-- LowerAtomics.h - Lower atomic instructions ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Wrapper around LLVM's LowerAtomic pass.
///
//===----------------------------------------------------------------------===//

#ifndef LOWER_ATOMICS_H
#define LOWER_ATOMICS_H

#include "llvm/Pass.h"
#include "llvm/Transforms/Scalar/LowerAtomic.h"

class LowerAtomicWrapper : public llvm::FunctionPass {
public:
  static char ID;
  LowerAtomicWrapper() : llvm::FunctionPass(ID) {}
  virtual bool runOnFunction(llvm::Function &) override;

private:
  llvm::LowerAtomicPass Impl;
};

namespace llvm {
void initializeLowerAtomicWrapperPass(PassRegistry &);
} // namespace llvm

#endif
