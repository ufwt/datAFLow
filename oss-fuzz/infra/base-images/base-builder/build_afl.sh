#!/bin/bash -eux

export PATH="$SRC/llvm/install/bin:$PATH"
export LD_LIBRARY_PATH="$SRC/llvm/install/lib:${LD_LIBRARY_PATH-}"

if [[ ! -d "AFL" ]]; then
  git clone --depth=1 https://github.com/google/AFL
fi

CC=clang CXX=clang++ make -j -C $SRC/AFL
CC=clang CXX=clang++ make -j -C $SRC/AFL/llvm_mode
