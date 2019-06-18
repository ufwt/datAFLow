//===-- TagDynamicAllocsUtils.h - Utilities -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utility functions for tagging dynamic memory allocation functions.
///
//===----------------------------------------------------------------------===//

#ifndef FUZZALLOC_TAG_DYNAMIC_ALLOCS_UTIL_H
#define FUZZALLOC_TAG_DYNAMIC_ALLOCS_UTIL_H

#include <map>

#include "llvm/ADT/SmallPtrSet.h"

namespace llvm {
class GlobalVariable;
class Function;
class Module;
class StructType;
} // namespace llvm

/// A struct type and an offset into that struct
using StructElement = std::pair<const llvm::StructType *, unsigned>;

/// Tagged global variables and struct elements that must be serialized so that
/// they this information can be shared between modules during an LLVM
/// compilation run
struct TaggedValues {
  const llvm::SmallPtrSet<const llvm::GlobalVariable *, 16> GlobalVariables;
  const std::map<StructElement, const llvm::Function *> StructMap;

  TaggedValues(const llvm::SmallPtrSet<const llvm::GlobalVariable *, 16> &GVs,
               const std::map<StructElement, const llvm::Function *> &Structs)
      : GlobalVariables(GVs), StructMap(Structs) {}
};

/// Read tagged global variables and struct elements from the fuzzalloc log file
std::unique_ptr<TaggedValues> getTaggedValues(const llvm::Module &);

/// Save tagged global variables and struct elements from the fuzzalloc log file
bool saveTaggedValues(const TaggedValues &);

#endif // FUZZALLOC_TAG_DYNAMIC_ALLOCS_UTIL_H
