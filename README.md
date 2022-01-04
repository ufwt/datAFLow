# datAFLow

## Motivation

Most popular fuzzers (e.g., AFL, libFuzzer, honggfuzz, VUzzer, etc.) are
_code-coverage_ guided. This means that they select and mutate seeds with the
aim of maximising the code coverage of the program under test (PUT). Seeds that
lead to new code being executed are therefore prioritised over other seeds.

## How does AFL record code coverage?

AFL (a popular open-source fuzzer) performs compile-time source instrumentation
to insert code that tracks _edge_ coverage (which is more accurate than _block_
coverage). This edge coverage information is stored in a bitmap (default size
64KB), where each byte in the bitmap represents the hit count of a specific
edge. The code injected at each edge is essentially equivalent to (from the
AFL documentation):

```
cur_location = <COMPILE_TIME_RANDOM>;
bitmap[cur_location ^ prev_location]++;
prev_location = cur_location >> 1;
```

Note that the last right-shift operation is performed to preserve the
directionality of edges (without this, `A -> B` would be indistinguishable from
`B -> A`).

## What issues may arise when using code coverage?

Take the following example (seems to be a classic example, used in a number of
lecture slides):

```
            1
           / \
 x = ...; 2   3
           \ /
            4
           / \
          5   6 ... = x;
           \ /
            7
```

We may execute the following two paths:

```
1 -> 2 -> 4 -> 5 -> 7
1 -> 3 -> 4 -> 6 - >7
```

Fuzzers guided by AFL may mark this code as being fully explored and
deprioritise seeds that further execute this code. However, there is a chance
we may miss any interesting behaviour that is exercised in the relationship
between the _definition_ of `x` in statement `2` and the _usage_ of `x` in
statement `6`.

## Data-flow coverage

Data-flow coverage seeks to capture _use-def_ relationships such as the one
outlined in the previous section. In "data-flow guided fuzzing", seed selection
is based on the ways in which values are associated with variables, and how
these associations can affect the execution of the program.

## `fuzzalloc`

A low-fat pointer memory allocator for fuzzing. Stores useful metadata in the
upper bits of allocated memory addresses. This achieved via two mechanisms: a
runtime replacement for malloc and friends, `libfuzzalloc`, and a set of LLVM
passes to transform your target to use `libfuzzalloc`.

Note that the `FUZZALLOC_SRC` variable refers to this directory.

## Building

The `datAFLow` fuzzer requires a custom version of clang. Once this is built,
the `fuzzalloc` toolchain can be built.

### Patching clang

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

### With AddressSanitizer (ASan)

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

### Building `fuzzalloc`

```bash
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAFL_PATH=/path/to./afl/source $FUZZALLOC_SRC
make -j
```

To build the runtime with AFL++ support, set the `-DAFL_INSTRUMENT=On` CMake
flag.

## Usage

`libfuzzalloc` is a drop-in replacement for malloc and friends. When using
gcc, it's safest to pass in the flags

```bash
-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
```

All you have to do is link your target with `-lfuzzalloc`.

### Instrumenting a Target

The `dataflow-cc` (and `dataflow-cc++`) tools can be used as dropin replacements
for `clang` (and `clang++`).

Note that this typically requires running `dataflow-preprocess` before running
`dataflow-cc` to collect the allocation sites to tag.

### `dataflow-preprocess`

If the target uses custom memory allocation routines (i.e., wrapping `malloc`,
`calloc`, etc.), then a [special case list](https://clang.llvm.org/docs/SanitizerSpecialCaseList.html)
containing a list of these routines should be provided to `dataflow-preprocess`.
Doing so ensures that dynamically-allocated variable _def_ sites are
appropriately tagged. The list is provided via the `FUZZALLOC_MEM_FUNCS`
environment variable; i.e., `FUZZALLOC_MEM_FUNCS=/path/to/special/case/list`.
The special case list must be formatted as:

```
[fuzzalloc]
fun:malloc_wrapper
fun:calloc_wrapper
fun:realloc_wrapper
```

The locations of variable tag sites are stored in a file specified by the
`FUZZALLOC_TAG_LOG` environment variable.

### `dataflow-cc`

`dataflow-cc` is a drop-in replacement for `clang`. To use the tag list
generated by `dataflow-preprocess`, set it in the `FUZZALLOC_TAG_LOG`
environment variable (e.g., `FUZZALLOC_TAG_LOG=/path/to/tags`).

Other useful environment variables include:

* `FUZZALLOC_FUZZER`: Sets the fuzzer instrumentation to use. Valid fuzzers
include: `debug-log` (logging to `stderr`. This requires `fuzzalloc` be built
in debug mode; i.e., with `-DCMAKE_BUILD_TYPE=Debug`), `AFL`, and `libfuzzer`.

* `FUZZALLOC_SENSITIVITY`: Sets the _use_ site sensitivity. Valid sensitivities
are: `mem-read`, `mem-write`, `mem-access`, `mem-read-offset`,
`mem-write-offset`, and `mem-access-offset`.

### With libFuzzer (Experimental)

The following flags are added to libFuzzer:

* `use_dataflow`: Enable dataflow-based coverage
* `print_dataflows`: Print out covered def/use chains
* `job_prefix`: fuzz-JOB.log prefix
