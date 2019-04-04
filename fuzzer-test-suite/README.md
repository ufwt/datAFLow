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

```bash
FUZZING_ENGINE="datAFLow" /path/to/fuzzer-test-suite/build.sh TARGET
```

Where `TARGET` is one of the directories inside the fuzzer-test-suite directory
(e.g., libpng-1.2.56).

# Fuzzing example (libxml)

To fuzz libxml from the fuzzer test suite, assuming that the fuzzer test suite
has been setup at `$FUZZER_TEST_SUITE`:

1. Build libxml2

```bash
FUZZING_ENGINE="datAFLow" $FUZZER_TEST_SUITE/build.sh libxml2-v2.9.2
```

2. Get some seeds

```bash
git clone https://github.com/MozillaSecurity/fuzzdata.git `pwd`/fuzzdata
export $FUZZDATA=`pwd`/fuzzdata
```

3. Start the fuzzer!

```bash
export LD_LIBRARY_PATH=$FUZZER_TEST_SUITE/fuzzalloc-build/src/malloc:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$FUZZER_TEST_SUITE/RUNDIR-datAFLow-libxml2-v2.9.2/BUILD/.libs/:$LD_LIBRARY_PATH

$FUZZER_TEST_SUITE/AFL/afl-fuzz -i $FUZZDATA/samples/xml -o ./libxml2-fuzz-out \
    -m none -t 1000+ -- \
    $FUZZER_TEST_SUITE/RUNDIR-datAFLow-libxml2-v2.9.2/BUILD/.libs/xmllint -o /dev/null @@
```
