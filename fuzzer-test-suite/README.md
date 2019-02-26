# fuzzer-test-suite

This set of scripts will build the Google
[fuzzer test suite](https://github.com/google/fuzzer-test-suite) with the
datAFLow instrumentation.

# Usage

Run `setup.sh` to download the test suite and required tools (AFL and LLVM's
compiler-rt). You can set the `AFL_PATH` environment variable if you already
have AFL. Set the `DEBUG` environment variable (e.g., to 1) to build a debug
version of libfuzzalloc.
