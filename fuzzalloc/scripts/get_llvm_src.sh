#!/bin/bash
#
# This script will place everything in the current working directory

set -ex

# Get LLVM
wget --retry-connrefused http://releases.llvm.org/7.0.0/llvm-7.0.0.src.tar.xz
tar -xf llvm-7.0.0.src.tar.xz
mv llvm-7.0.0.src/* .
rm llvm-7.0.0.src.tar.xz

# Get Clang
wget --retry-connrefused http://releases.llvm.org/7.0.0/cfe-7.0.0.src.tar.xz
tar -xf cfe-7.0.0.src.tar.xz
mv cfe-7.0.0.src tools/clang
rm cfe-7.0.0.src.tar.xz

# Get compiler-rt
wget --retry-connrefused http://releases.llvm.org/7.0.0/compiler-rt-7.0.0.src.tar.xz
tar -xf compiler-rt-7.0.0.src.tar.xz
mv compiler-rt-7.0.0.src projects/compiler-rt
rm compiler-rt-7.0.0.src.tar.xz
