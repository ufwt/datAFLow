//===-- Common.cpp - LLVM transform utils ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality.
///
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/Operator.h"

#include "Common.h"

using namespace llvm;

Value *GetUnderlyingObjectThroughLoads(Value *V, const DataLayout &DL,
                                       unsigned MaxLookup) {
  if (!V->getType()->isPointerTy())
    return V;

  for (unsigned Count = 0; MaxLookup == 0 || Count < MaxLookup; ++Count) {
    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast ||
               Operator::getOpcode(V) == Instruction::Load) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
      if (GA->isInterposable()) {
        return V;
      }
      V = GA->getAliasee();
    } else if (isa<AllocaInst>(V)) {
      // An alloca can't be further simplified.
      return V;
    } else {
      if (auto CS = CallSite(V)) {
        // CaptureTracking can know about special capturing properties of some
        // intrinsics like launder.invariant.group, that can't be expressed with
        // the attributes, but have properties like returning aliasing pointer.
        // Because some analysis may assume that nocaptured pointer is not
        // returned from some special intrinsic (because function would have to
        // be marked with returns attribute), it is crucial to use this function
        // because it should be in sync with CaptureTracking. Not using it may
        // cause weird miscompilations where 2 aliasing pointers are assumed to
        // noalias.
        if (auto *RP = getArgumentAliasingToReturnedPointer(CS)) {
          V = RP;
          continue;
        }
      }

      // See if InstructionSimplify knows any relevant tricks.
      if (Instruction *I = dyn_cast<Instruction>(V))
        // TODO: Acquire a DominatorTree and AssumptionCache and use them.
        if (Value *Simplified = SimplifyInstruction(I, {DL, I})) {
          V = Simplified;
          continue;
        }

      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  }

  return V;
}
