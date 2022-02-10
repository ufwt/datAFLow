//===-- LowerMemIntrinics .h - Lower llvm.mem* intrinsics -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Wrapper around LLVM's LowerMemIntrinics functionality.
///
//===----------------------------------------------------------------------===//

#ifndef LOWER_MEM_INTRINSICS_H
#define LOWER_MEM_INTRINSICS_H

#include "llvm/Pass.h"

class LowerMemIntrinsics : public llvm::FunctionPass {
public:
  static char ID;
  LowerMemIntrinsics() : llvm::FunctionPass(ID) {}
  virtual bool runOnFunction(llvm::Function &) override;
  virtual void getAnalysisUsage(llvm::AnalysisUsage &) const override;
};

namespace llvm {
void initializeLowerMemIntrinsicsPass(PassRegistry &);
} // namespace llvm

#endif
