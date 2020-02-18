# Fuzzing Targets

This directory contains a number of Dockerfiles for building a variety of
fuzzing targets.

## Setup

Run the following to setup:

```bash
mkdir -p /path/to/experiment/dir
cd /path/to/experiment/dir
/path/to/datAFLow/fuzzing-targets/setup.sh
```

This will setup the experiments directory and build all of the Docker images.

## Fuzzing

```bash
# Run everything
/path/to/this/dir/fuzz.sh ${TARGET}
```

## Adding targets

1. Create a Docker file that describes how to build the target. It should follow
   the same format as the existing targets (i.e., with build dirs `${TARGET}-afl`,
   `${TARGET}-datAFLow-access`, etc.)
2. Create a `fuzz-config.sh` with:
   - `EXE` variable with the name of the binary to run
   - `EXE_OPTS` variable with the runtime options to use (including `@@` for AFL)
