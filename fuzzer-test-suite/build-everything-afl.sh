#!/bin/bash
# Copyright 2017 Google Inc. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 (the "License");

export FUZZING_ENGINE="afl"
export ASAN_ENABLE=1

. $(dirname $0)/common.sh

export ABS_SCRIPT_DIR=$(readlink -f ${SCRIPT_DIR})
export PARENT_DIR="${ABS_SCRIPT_DIR}/../ALL_BENCHMARKS-${FUZZING_ENGINE}"

rm -rf ${PARENT_DIR}
mkdir ${PARENT_DIR}
echo "Created top directory ${PARENT_DIR}"

build_target() {
  file_name="$(basename ${1})"

  [[ ! -d ${1} ]] && return
  [[ ! -e ${1}build.sh ]] && return

  echo "Running build ${file_name}"
  cd ${PARENT_DIR}
  ${ABS_SCRIPT_DIR}/build.sh "${file_name}" > ${file_name}.log 2>&1
}

export -f build_target
parallel -j10 build_target ::: ${ABS_SCRIPT_DIR}/*/
