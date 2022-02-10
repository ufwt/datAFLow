//===-- LowerAtomics.cpp - Lower atomic instructions ----------------------===//
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

#include "LowerAtomics.h"

using namespace llvm;

#define DEBUG_TYPE "lower-atomics"

// Adapted from `LowerAtomicLegacyPass`
bool LowerAtomicWrapper::runOnFunction(Function &F) {
  FunctionAnalysisManager DummyFAM;
  auto PA = Impl.run(F, DummyFAM);
  return !PA.areAllPreserved();
}

char LowerAtomicWrapper::ID = 0;

INITIALIZE_PASS(LowerAtomicWrapper, DEBUG_TYPE, "Lower atomic intrinsics",
                false, false)
