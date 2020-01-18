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
docker build -t dataflow/bison bison/
```

# Fuzzing

First, we need to pull out the files we need (don't fuzz inside the container).

```bash
# Run the container
docker run -ti --name bison-fuzz dataflow/bison

# Setup experiment dir
mkdir -p /path/to/experiment/dir/
cd /path/to/experiment/dir
git clone --depth=1 https://github.com/google/AFL
make -C AFL -j && make -C AFL/llvm_mode -j
git clone --depth=1 https://github.com/puppet-meteor/MOpt-AFL
make -C MOpt-AFL/MOpt-AFL\ V1.0/ -j && make -C MOpt-AFL/MOpt-AFL\ V1.0/llvm_mode -j

# Make target directory
mkdir -p bison

# Copy out files
docker cp bison-fuzz:/root/bison-afl bison/
docker cp bison-fuzz:/root/bison-datAFLow-access bison/
docker cp bison-fuzz:/root/bison-datAFLow-access-idx bison/
docker cp bison-fuzz:/root/seeds bison/

# Run the experiment
/path/to/this/dir/fuzz.sh bison
```

Once everything is finished:

```bash
# Stop the container
docker stop bison-fuzz
docker rm bison-fuzz
```
