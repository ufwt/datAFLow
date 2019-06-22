#!/bin/bash
# Copyright 2017 Google Inc. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

# Don't allow to call these scripts from their directories.
[ -e $(basename $0) ] && echo "PLEASE USE THIS SCRIPT FROM ANOTHER DIR" && exit 1

# Ensure that fuzzing engine, if defined, is valid
FUZZING_ENGINE=${FUZZING_ENGINE:-"datAFLow"}
POSSIBLE_FUZZING_ENGINE="libfuzzer afl coverage fsanitize_fuzzer hooks datAFLow tags"
!(echo "$POSSIBLE_FUZZING_ENGINE" | grep -w "$FUZZING_ENGINE" > /dev/null) && \
  echo "USAGE: Error: If defined, FUZZING_ENGINE should be one of the following:
  $POSSIBLE_FUZZING_ENGINE. However, it was defined as $FUZZING_ENGINE" && exit 1

SCRIPT_DIR=$(dirname $(realpath -s $0))
EXECUTABLE_NAME_BASE=$(basename $SCRIPT_DIR)-${FUZZING_ENGINE}
LIBFUZZER_SRC=${LIBFUZZER_SRC:-$(dirname $(dirname $SCRIPT_DIR))/Fuzzer}
AFL_SRC=${AFL_SRC:-$(dirname $(dirname $SCRIPT_DIR))/AFL}
COVERAGE_FLAGS="-O0 -fsanitize-coverage=trace-pc-guard"
FUZZ_CXXFLAGS="-O2 -fno-omit-frame-pointer -gline-tables-only -fsanitize=address -fsanitize-address-use-after-scope -fsanitize-coverage=trace-pc-guard,trace-cmp,trace-gep,trace-div"
CORPUS=CORPUS-$EXECUTABLE_NAME_BASE
JOBS=${JOBS:-"8"}

export LIB_FUZZING_ENGINE="libFuzzingEngine-${FUZZING_ENGINE}.a"

if [[ $FUZZING_ENGINE == "fsanitize_fuzzer" ]]; then
  FSANITIZE_FUZZER_FLAGS="-O2 -fno-omit-frame-pointer -gline-tables-only -fsanitize=address,fuzzer-no-link -fsanitize-address-use-after-scope"
  export CFLAGS=${CFLAGS:-$FSANITIZE_FUZZER_FLAGS}
  export CXXFLAGS=${CXXFLAGS:-$FSANITIZE_FUZZER_FLAGS}
elif [[ $FUZZING_ENGINE == "coverage" ]]; then
  export CFLAGS=${CFLAGS:-$COVERAGE_FLAGS}
  export CXXFLAGS=${CXXFLAGS:-$COVERAGE_FLAGS}
elif [[ $FUZZING_ENGINE == "datAFLow" ]]; then
  export FUZZALLOC_BUILD_DIR=${FUZZALLOC_BUILD_DIR:-$(dirname $SCRIPT_DIR)/fuzzalloc-build}
  export AFL_PATH=${AFL_SRC}
  export LD_LIBRARY_PATH="${FUZZALLOC_BUILD_DIR}/src/runtime/malloc/:${LD_LIBRARY_PATH}"

  if [ -z $FUZZALLOC_TAG_LOG ]; then
    echo "Error: FUZZALLOC_TAG_LOG environment variable not specified" && exit 1
  elif [ ! -f $FUZZALLOC_TAG_LOG ]; then
    echo "Error: Invalid tag log file in $FUZZALLOC_TAG_LOG" && exit 1
  fi

  export LLVM_CC_NAME="${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-clang-fast"
  export LLVM_CXX_NAME="${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-clang-fast++"

  export CC=${CC:-${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-clang-fast}
  export CXX=${CXX:=${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-clang-fast++}

  export CFLAGS="-O2 -fno-omit-frame-pointer -gline-tables-only"
  if [ ! -z $ASAN_ENABLE ]; then
    echo "ASan enabled"
    export CFLAGS="$CFLAGS -fsanitize=address -fsanitize-address-use-after-scope"
    export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:symbolize=1:allocator_may_return_null=1"
  fi
  export CXXFLAGS=${CFLAGS}
  export LIBS="-L${FUZZALLOC_BUILD_DIR}/src/runtime/malloc -lfuzzalloc"
elif [[ $FUZZING_ENGINE == "tags" ]]; then
  export FUZZALLOC_BUILD_DIR=${FUZZALLOC_BUILD_DIR:-$(dirname $SCRIPT_DIR)/fuzzalloc-build}

  if [ -f "${SCRIPT_DIR}/whitelist.txt" ]; then
    export FUZZALLOC_WHITELIST="${SCRIPT_DIR}/whitelist.txt"
  fi

  if [ -z $FUZZALLOC_TAG_LOG ]; then
    echo "Error: FUZZALLOC_TAG_LOG environment variable not specified. This must be an absolute path" && exit 1
  fi

  export LLVM_CC_NAME="${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-collect-tag-sites"
  export LLVM_CXX_NAME="${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-collect-tag-sites++"

  export CC=${CC:-${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-collect-tag-sites}
  export CXX=${CXX:=${FUZZALLOC_BUILD_DIR}/src/tools/dataflow-collect-tag-sites}

  export CFLAGS="-O2 -fno-omit-frame-pointer -gline-tables-only"
  export CXXFLAGS=${CFLAGS}
elif [[ $FUZZING_ENGINE == "afl" ]]; then
  export AFL_PATH=${AFL_SRC}

  export CC="${AFL_PATH}/afl-clang-fast"
  export CXX="${AFL_PATH}/afl-clang-fast++"

  export CFLAGS="-O2 -fno-omit-frame-pointer -gline-tables-only"
  if [ ! -z $ASAN_ENABLE ]; then
    echo "ASan enabled"
    export CFLAGS="$CFLAGS -fsanitize=address -fsanitize-address-use-after-scope"
    export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:symbolize=1:allocator_may_return_null=1"
  fi
  export CXXFLAGS=${CFLAGS}
else
  export CFLAGS=${CFLAGS:-"$FUZZ_CXXFLAGS"}
  export CXXFLAGS=${CXXFLAGS:-"$FUZZ_CXXFLAGS"}
fi

export CC=${CC:-"clang"}
export CXX=${CXX:-"clang++"}
export CPPFLAGS=${CPPFLAGS:-"-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"}

get_git_revision() {
  GIT_REPO="$1"
  GIT_REVISION="$2"
  TO_DIR="$3"
  [ ! -e $TO_DIR ] && git clone $GIT_REPO $TO_DIR && (cd $TO_DIR && git reset --hard $GIT_REVISION)
}

get_git_tag() {
  GIT_REPO="$1"
  GIT_TAG="$2"
  TO_DIR="$3"
  [ ! -e $TO_DIR ] && git clone $GIT_REPO $TO_DIR && (cd $TO_DIR && git checkout $GIT_TAG)
}

get_svn_revision() {
  SVN_REPO="$1"
  SVN_REVISION="$2"
  TO_DIR="$3"
  [ ! -e $TO_DIR ] && svn co -r$SVN_REVISION $SVN_REPO $TO_DIR
}

build_datAFLow() {
  clang++ $CXXFLAGS -std=c++11 -O2 -c ${LIBFUZZER_SRC}/afl/afl_driver.cpp -I$LIBFUZZER_SRC
  ar r $LIB_FUZZING_ENGINE afl_driver.o
  rm *.o
}

build_tags() {
  $CC -O2 -c $LIBFUZZER_SRC/standalone/StandaloneFuzzTargetMain.c
  ar rc $LIB_FUZZING_ENGINE StandaloneFuzzTargetMain.o
}

build_afl() {
  $CXX $CXXFLAGS -std=c++11 -O2 -c ${LIBFUZZER_SRC}/afl/afl_driver.cpp -I$LIBFUZZER_SRC
  ar r $LIB_FUZZING_ENGINE afl_driver.o
  rm *.o
}

build_libfuzzer() {
  $LIBFUZZER_SRC/build.sh
  mv libFuzzer.a $LIB_FUZZING_ENGINE
}

# Uses the capability for "fsanitize=fuzzer" in the current clang
build_fsanitize_fuzzer() {
  LIB_FUZZING_ENGINE="-fsanitize=fuzzer"
}

# This provides a build with no fuzzing engine, just to measure coverage
build_coverage () {
  $CC -O2 -c $LIBFUZZER_SRC/standalone/StandaloneFuzzTargetMain.c
  ar rc $LIB_FUZZING_ENGINE StandaloneFuzzTargetMain.o
}

# Build with user-defined main and hooks.
build_hooks() {
  LIB_FUZZING_ENGINE=libFuzzingEngine-hooks.o
  $CXX -c $HOOKS_FILE -o $LIB_FUZZING_ENGINE
}

build_fuzzer() {
  echo "Building with $FUZZING_ENGINE"
  build_${FUZZING_ENGINE}
}

