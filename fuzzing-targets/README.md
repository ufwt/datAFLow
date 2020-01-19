# Fuzzing Targets

This directory contains a number of Dockerfiles for building a variety of
fuzzing targets.

## Building

First compile the base Docker image:

```bash
docker build -t dataflow/base . --build-arg SSH_PRIVATE_KEY="$(cat ~/.ssh/id_rsa)"
```

Then build the image for the target you want to fuzz, e.g.:

```bash
export TARGET="bison"
docker build -t dataflow/${TARGET} ${TARGET}
```

# Fuzzing

```bash
# Setup experiment dir
mkdir -p /path/to/experiment/dir/
cd /path/to/experiment/dir
git clone --depth=1 https://github.com/google/AFL
make -C AFL -j && make -C AFL/llvm_mode -j
git clone --depth=1 https://github.com/puppet-meteor/MOpt-AFL
make -C MOpt-AFL/MOpt-AFL\ V1.0/ -j && make -C MOpt-AFL/MOpt-AFL\ V1.0/llvm_mode -j

# Run everything
/path/to/this/dir/fuzz.sh ${TARGET}
```

# Adding targets

1. Create a Docker file that describes how to build the target. It should follow
   the same format as the existing targets (i.e., with build dirs `${TARGET}-afl`,
   `${TARGET}-datAFLow-access`, etc.)
2. Create a `fuzz-config.sh` with:
   - `EXE` variable with the name of the binary to run
   - `EXE_OPTS` variable with the runtime options to use (including `@@` for AFL)
