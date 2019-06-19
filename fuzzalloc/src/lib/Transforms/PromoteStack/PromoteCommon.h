//===-- PromoteCommon.h - Promote static arrays to mallocs ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Common functionality for static array/struct promotion.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_PROMOTE_COMMON_H
#define FUZZALLOC_PROMOTE_COMMON_H

namespace llvm {
class Instruction;
class Value;
} // namespace llvm

/// Insert a call to \c free for the given alloca
void insertFree(llvm::Value *, llvm::Instruction *);

#endif // FUZZALLOC_PROMOTE_COMMON_H
