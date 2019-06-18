//===-- TagDynamicAllocsUtils.cpp - Utilities -----------------------------===//
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

#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "TagDynamicAllocsUtil.h"

using namespace llvm;

static const char *const LogDirEnvVar = "FUZZALLOC_LOG_DIR";
static const char *const TagLogFileName = "tagged_values.txt";
static const char *const LogSeparator = ":";
static const char *const GlobalVariableLogPrefix = "gv";
static const char *const StructLogPrefix = "struct";

static std::string getLogDir() {
  std::string Result;

  char *LogDir = getenv(LogDirEnvVar);
  if (LogDir) {
    Result = LogDir;
  } else {
    SmallString<32> Dir;
    sys::path::system_temp_directory(true, Dir);
    Result = Dir.str();
  }

  return Result;
}

std::unique_ptr<TaggedValues> getTaggedValues(const Module &M) {
  std::string LogDir = getLogDir();
  SmallString<32> LogPath(LogDir);
  sys::path::append(LogPath, TagLogFileName);

  auto InputOrErr = MemoryBuffer::getFile(LogPath);
  if (InputOrErr.getError()) {
    return nullptr;
  }

  SmallVector<StringRef, 16> Lines;
  StringRef InputData = InputOrErr.get()->getBuffer();
  InputData.split(Lines, '\n', /* MaxSplit */ -1, /* KeepEmpty */ false);

  SmallPtrSet<const GlobalVariable *, 16> GVs;
  std::map<StructElement, const Function *> StructMap;

  for (auto Line : Lines) {
    if (Line.startswith(GlobalVariableLogPrefix)) {
      // Parse global variable
      SmallVector<StringRef, 2> GVString;
      Line.split(GVString, LogSeparator);

      const auto *GV = M.getGlobalVariable(GVString[1]);
      if (!GV) {
        continue;
      }

      GVs.insert(GV);
    } else if (Line.startswith(StructLogPrefix)) {
      // Parse struct mapping
      SmallVector<StringRef, 4> StructMapString;
      Line.split(StructMapString, LogSeparator);

      auto *StructTy = M.getTypeByName("struct." + StructMapString[1].str());
      if (!StructTy) {
        continue;
      }

      unsigned Offset;
      if (StructMapString[2].getAsInteger(10, Offset)) {
        continue;
      }

      auto *F = M.getFunction(StructMapString[3]);
      if (!F) {
        continue;
      }

      StructMap.emplace(std::make_pair(StructTy, Offset), F);
    }
  }

  return llvm::make_unique<TaggedValues>(GVs, StructMap);
}

bool saveTaggedValues(const TaggedValues &TVs) {
  std::string LogDir = getLogDir();
  SmallString<32> LogPath(LogDir);
  sys::path::append(LogPath, TagLogFileName);

  std::error_code EC;
  raw_fd_ostream Output(
      LogPath, EC, sys::fs::OpenFlags::OF_Text | sys::fs::OpenFlags::OF_Append);
  if (EC) {
    return false;
  }

  // Save global variables
  for (auto *GV : TVs.GlobalVariables) {
    assert(GV->hasName());
    Output << GlobalVariableLogPrefix << LogSeparator << GV->getName() << '\n';
  }

  // Save struct mappings
  for (auto StructWithFunc : TVs.StructMap) {
    auto *StructTy = StructWithFunc.first.first;
    unsigned Offset = StructWithFunc.first.second;
    auto *F = StructWithFunc.second;

    assert(StructTy->hasName());
    assert(F->hasName());

    Output << StructLogPrefix << LogSeparator << StructTy << LogSeparator
           << Offset << LogSeparator << F->getName() << '\n';
  }

  return true;
}
