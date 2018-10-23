# fuzzalloc

A memory allocator for use in fuzzing. Stores useful metadata in the upper
bits of allocated memory addresses. This achieved via two mechanisms: a runtime
replacement for malloc and friends, `libfuzzalloc`, and a set of
LLVM passes to transform your program under test (PUT) to use `libfuzzalloc`.

# Build

```console
mkdir build
cd build
cmake ..
make -j
```

# Usage

`libfuzzalloc` is a drop-in replacement for malloc and friends. When using
gcc, it's safest to pass in the flags

```console
-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
```

All you have to do is link your PUT with `-lfuzzalloc`.
