# fuzzalloc

A memory allocator for use in fuzzing. Stores useful metadata in the upper
bits of allocated memory addresses. This achieved via two mechanisms: a runtime
replacement for malloc and friends, `libfuzzalloc`, and a set of
LLVM passes to transform your target to use `libfuzzalloc`.

# Build

```bash
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAFL_PATH=/path/to./afl/source ..
make -j
```

Add `-DAFL_INSTRUMENTATION=On` to compile `libfuzzalloc` with AFL support.

We use LLVM 7.0 (available from http://releases.llvm.org/download.html#7.0.0).

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

To build the custom ASan:

```bash
# Get the LLVM source code and update the ASan source code
mkdir llvm
cd llvm
./scripts/get_llvm_source.sh
./scripts/update_asan_source.sh

# Build and install LLVM/clang/etc.
mkdir build
mkdir install
cd build
# If debugging you can also add -DCMAKE_BUILD_TYPE=Debug -DCOMPILER_RT_DEBUG=On
cmake .. -DFUZZALLOC_ASAN=On -DLIBFUZZALLOC_PATH=/path/to/libfuzzalloc.so   \
    -DLLVM_BUILD_EXAMPLES=Off -DLLVM_INCLUDE_EXAMPLES=Off                   \
    -DLLVM_PARALLEL_LINK_JOBS=1 -DLLVM_TARGETS_TO_BUILD="X86"               \
    -DLLVM_ENABLE_ASSERTIONS=True -DCMAKE_INSTALL_PREFIX=$(realpath ../install)
make -j
make install

# Add the install directory to your path so that you use the correct clang
export PATH=$(realpath ../install):$PATH
```

Note that after building LLVM with the custom ASan, you will have to rebuild
fuzzalloc with the new clang/clang++ (found under `install/bin`).
