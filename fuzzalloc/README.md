# fuzzalloc

A low-fat pointer memory allocator for fuzzing. Stores useful metadata in the
upper bits of allocated memory addresses. This achieved via two mechanisms: a
runtime replacement for malloc and friends, `libfuzzalloc`, and a set of LLVM
passes to transform your target to use `libfuzzalloc`.

Note that the `FUZZALLOC_SRC` variable refers to this directory.

# Patching clang
`fuzzalloc` requires a patch to the clang compiler to disable turning constant
arrays into packed constant structs.

To build the custom clang:

```bash
# Get the LLVM source code and update the clang source code
mkdir llvm
cd llvm
$FUZZALLOC_SRC/scripts/get_llvm_src.sh
$FUZZALLOC_SRC/scripts/update_clang_src.sh

# Build and install LLVM/clang/etc.
mkdir build
mkdir install
cd build
# If debugging you can also add -DCMAKE_BUILD_TYPE=Debug -DCOMPILER_RT_DEBUG=On
# Note that if you're going to use gclang, things seem to work better if you use
# the gold linker (https://llvm.org/docs/GoldPlugin.html)
cmake ../llvm -DLLVM_ENABLE_PROJECTS="clang;compiler-rt"        \
    -DLLVM_BUILD_EXAMPLES=Off -DLLVM_INCLUDE_EXAMPLES=Off       \
    -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_INSTALL_PREFIX=$(realpath ../install)
cmake --build .
cmake --build . --target install

# Add the install directory to your path so that you use the correct clang
export PATH=$(realpath ../install):$PATH
```

## With AddressSanitizer (ASan)

Fuzzing is typically performed in conjunction with a
[sanitizer](https://github.com/google/sanitizers/wiki) so that "silent" bugs can
be uncovered. Sanitizers such as
[ASan](https://github.com/google/sanitizers/wiki/AddressSanitizer) typically
hook and replace dynamic memory allocation routines such as `malloc`/`free` so
that they can detect buffer over/under flows, use-after-frees, etc.
Unfortunately, this means that we lose the ability to track dataflow (as we
rely on the memory allocator to do this). Therefore, we must use a custom
version of ASan in order to (a) detect bugs and (b) track dataflow.

To build the custom ASan, run the following after running `get_llvm_src.sh` and
`update_clang_src.sh` above:

```bash
cd llvm
$FUZZALLOC_SRC/scripts/update_compiler_rt_src.sh
$FUZZALLOC_SRC/scripts/update_llvm_src.sh

# Build and install LLVM/clang/etc.
cd build
# If debugging you can also add -DCMAKE_BUILD_TYPE=Debug -DCOMPILER_RT_DEBUG=On
cmake ../llvm -DLLVM_ENABLE_PROJECTS="clang;compiler-rt"                \
    -DFUZZALLOC_ASAN=On -DLIBFUZZALLOC_PATH=/path/to/libfuzzalloc.so    \
    -DLLVM_BUILD_EXAMPLES=Off -DLLVM_INCLUDE_EXAMPLES=Off               \
    -DLLVM_TARGETS_TO_BUILD="X86" -DCMAKE_INSTALL_PREFIX=$(realpath ../install)
cmake --build .
cmake --build . --target install

# Make sure the install path is available in $PATH
```

Note that after building LLVM with the custom ASan, you will have to rebuild
fuzzalloc with the new clang/clang++ (found under `install/bin`).

# Building `fuzzalloc`

```bash
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAFL_PATH=/path/to./afl/source $FUZZALLOC_SRC
make -j
```

Add `-DAFL_INSTRUMENTATION=On` to compile `libfuzzalloc` with AFL support.

# Usage

`libfuzzalloc` is a drop-in replacement for malloc and friends. When using
gcc, it's safest to pass in the flags

```bash
-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
```

All you have to do is link your target with `-lfuzzalloc`.

## With AFL

To use with AFL, the `dataflow-clang-fast` (and `dataflow-clang-fast++`) tools
can be used as dropin replacements for `clang` (and `clang++`).

Note that this typically requires running `dataflow-collect-tag-sites` before
running `dataflow-clang-fast` to collect the allocation sites to tag.

