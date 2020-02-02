# fuzzer-test-suite

This set of files will build the Google [fuzzer test
suite](https://github.com/google/fuzzer-test-suite) with the datAFLow
instrumentation.

# Usage

Run `setup.sh` (from the directory where you want the test suite to be) to
download the test suite and required tools (AFL and LLVM's
compiler-rt). You can set the and `COMPILER_RT_PATH` environment variables if
you already have AFL and LLVM's compiler-rt 7.0 respectively.

```bash
mkdir dataflow-test-suite
cd dataflow-test-suite
# Specify COMPILER_RT_PATH if appropriate
/path/to/datAFLow/fuzzer-test-suite/setup.sh
export DATAFLOW_TEST_DIR=$(pwd)
```

Do the following to build a target, where `TARGET` is the name of a
fuzzer-test-suite directory (e.g., freetype2-2017, libxml2-v2.9.2, etc.).

```bash
# Collect tag sites
FUZZALLOC_TAG_LOG=$DATAFLOW_TEST_DIR/TARGET-tag-sites.csv FUZZING_ENGINE=tags  \
    $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh TARGET

# Cleanup the log file
/path/to/datAFLow/scripts/remove_duplicate_lines.py                            \
    $DATAFLOW_TEST_DIR/TARGET-tag-sites.csv > deduped-tag-sites.csv
mv deduped-tag-sites.csv $DATAFLOW_TEST_DIR/TARGET-tag-sites.csv

# Build the target for datAFLow fuzzing, using the tag sites recorded in the
# CSV file
FUZZALLOC_TAG_LOG=$DATAFLOW_TEST_DIR/TARGET-tag-sites.csv                      \
    FUZZING_ENGINE=datAFLow $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh TARGET
```

Note that in addition to the `datAFLow` `FUZZING_ENGINE`, you can also use
vanilla AFL by setting `FUZZING_ENGINE=afl`.

## With AddressSanitizer (ASan)

If fuzzing with ASan (as described
[here](https://github.com/HexHive/datAFLow/tree/master/fuzzalloc#with-addresssanitizer-asan)),
you should set the `ASAN_ENABLE` environment variable when building a target.
This will:

 * Add the necessary ASan compiler flags; and
 * Set `ASAN_OPTIONS` in line with AFL: e.g., disabling leak detection, etc.

## With gclang

[gclang] is a tool for building whole-program LLVM bitcode files. This is useful
for building targets to fuzz with
[Angora](https://github.com/AngoraFuzzer/Angora).

1. Install gclang from https://github.com/SRI-CSL/gllvm
2. Build a target

```bash
FUZZING_ENGINE=gclang $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh            \
    libxml2-v2.9.2
```

## With Angora

[Angora](https://github.com/AngoraFuzzer/Angora) is a state-of-the-art
taint-based fuzzer.

1. Install Angora from https://github.com/AngoraFuzzer/Angora

```bash
git clone https://github/com/AngoraFuzzer/Angora.git $ANGORA_DIR
cd $ANGORA_DIR
./build/build.sh
```

2. Build a target

```bash
# Build track version
USE_TRACK=1 ANGORA_BUILD_DIR=$ANGORA_DIR/bin FUZZING_ENGINE=angora             \
    ANGORA_TAINT_RULE_LIST=$ANGORA_DIR/bin/rules/zlib_abilist.txt              \
    LDFLAGS="-L$ANGORA_DIR/bin/lib" LIBS="-lZlibRt -lz"                        \
    $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh libxml2-v2.9.2
mv RUNDIR-angora-libxml2-v2.9.2 RUNDIR-angora-track-libxml2-v2.9.2

# Build fast version
USE_FAST=1 ANGORA_BUILD_DIR=$ANGORA_DIR/bin FUZZING_ENGINE=angora              \
    ANGORA_TAINT_RULE_LIST=$ANGORA_DIR/bin/rules/zlib_abilist.txt              \
    LDFLAGS="-L$ANGORA_DIR/bin/lib" LIBS="-lZlibRt -lz"                        \
    $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh libxml2-v2.9.2
mv RUNDIR-angora-libxml2-v2.9.2 RUNDIR-angora-fast-libxml2-v2.9.2
```

# Fuzzing example (libxml)

To fuzz libxml from the fuzzer test suite:

1. Build libxml2

```bash
FUZZALLOC_TAG_LOG=$DATAFLOW_TEST_DIR/libxml2-v2.9.2-tag-sites.csv              \
    FUZZING_ENGINE=tags $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh          \
    libxml2-v2.9.2
/path/to/datAFLow/scripts/remove_duplicate_lines.py                            \
    $DATAFLOW_TEST_DIR/libxml2-v2.9.2-tag-sites.csv > deduped-tag-sites.csv
mv deduped-tag-sites.csv $DATAFLOW_TEST_DIR/libxml2-v2.9.2-tag-sites.csv
FUZZALLOC_TAG_LOG=$DATAFLOW_TEST_DIR/libxml2-v2.9.2-tag-sites.csv              \
    FUZZING_ENGINE=datAFLow $DATAFLOW_TEST_DIR/fuzzer-test-suite/build.sh      \
    libxml2-v2.9.2
```

2. Get some seeds

```bash
git clone https://github.com/MozillaSecurity/fuzzdata.git `pwd`/fuzzdata
export $FUZZDATA=`pwd`/fuzzdata
```

3. Start the fuzzer!

```bash
export LD_LIBRARY_PATH=$FUZZER_TEST_SUITE/fuzzalloc-release/src/runtime/malloc:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$FUZZER_TEST_SUITE/RUNDIR-datAFLow-libxml2-v2.9.2/BUILD/.libs/:$LD_LIBRARY_PATH

$FUZZER_TEST_SUITE/AFL/afl-fuzz -i $FUZZDATA/samples/xml -o ./libxml2-fuzz-out \
    -m none -t 1000+ --                                                        \
    $FUZZER_TEST_SUITE/RUNDIR-datAFLow-libxml2-v2.9.2/BUILD/.libs/xmllint      \
    -o /dev/null @@
```

Or for a longer campaign:

```bash
AFL_NO_UI=1 nohup timeout 24h $FUZZER_TEST_SUITE/AFL/afl-fuzz                  \
    -i $FUZZDATA/samples/xml -o ./libxml2-fuzz-out                             \
    -m none -t 1000+ --                                                        \
    $FUZZER_TEST_SUITE/RUNDIR-datAFLow-libxml2-v2.9.2/BUILD/.libs/xmllint      \
    -o /dev/null @@
```

# Automate fuzzing campaigns

Use the `run_experiments.py` script. This assumes that you have run one of the
`build-everything-*.sh` scripts.

```bash
./run_experiments.py -e datAFLow -a $FUZZER_TEST_SUITE/AFL          \
    -f $FUZZER_TEST_SUITE/fuzzer-test-suite -c fuzzer-config.yaml   \
    $FUZZER_TEST_SUITE/ALL_BENCHMARKS-datAFLow-asan
```
