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

```bash
# Run the container
docker run -ti --name bison-fuzz dataflow/bison
```

Once everything is finished:

```bash
# Stop the container
docker stop bison-fuzz
docker rm bison-fuzz
```
