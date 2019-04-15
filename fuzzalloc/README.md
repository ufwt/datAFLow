# fuzzalloc

A memory allocator for use in fuzzing. Stores useful metadata in the upper
bits of allocated memory addresses. This achieved via two mechanisms: a runtime
replacement for malloc and friends, `libfuzzalloc`, and a set of
LLVM passes to transform your program under test (PUT) to use `libfuzzalloc`.

# Build

```bash
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..
make -j
```

We use LLVM 7 (available from http://releases.llvm.org/download.html#7.0.0).

To compile with AFL instrumentation, set the `AFL_INSTRUMENT` CMake flag (i.e.,
add `-DAFL_INSTRUMENT=On` to the `cmake` command above) and set the `AFL_PATH`
environment variable to the AFL source code directory.

# Usage

`libfuzzalloc` is a drop-in replacement for malloc and friends. When using
gcc, it's safest to pass in the flags

```bash
-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
```

All you have to do is link your PUT with `-lfuzzalloc`.

## With AFL

To use with AFL, the `dataflow-clang-fast` (and `dataflow-clang-fast++`) tools
can be used as dropin replacements for `clang` (and `clang++`).
