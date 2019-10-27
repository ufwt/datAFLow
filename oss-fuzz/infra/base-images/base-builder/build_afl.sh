#!/bin/bash -eux

export PATH="$SRC/llvm/install/bin:$PATH"
export LD_LIBRARY_PATH="$SRC/llvm/install/lib:${LD_LIBRARY_PATH-}"

CC=clang CXX=clang++ make -j -C $SRC/afl-2.52b
CC=clang CXX=clang++ make -j -C $SRC/afl-2.52b/llvm_mode
