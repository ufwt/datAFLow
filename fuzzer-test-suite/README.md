# fuzzer-test-suite

This set of scripts will build the Google
[fuzzer test suite](https://github.com/google/fuzzer-test-suite) with the
datAFLow instrumentation.

# Usage

Run `setup.sh` (from any directory where you want your test suite to end up) to
download the test suite and required tools (AFL and LLVM's
compiler-rt). You can set the `AFL_PATH` environment variable if you already
have AFL. Set the `DEBUG` environment variable (e.g., to 1) to build a debug
version of libfuzzalloc.

Run the following to build a target:

```console
FUZZING_ENGINE="datAFLow" /path/to/fuzzer-test-suite/build.sh TARGET
```

Where `TARGET` is one of the directories inside the fuzzer-test-suite directory
(e.g., libpng-1.2.56).
