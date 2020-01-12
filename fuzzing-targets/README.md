# Fuzzing Targets

This directory contains a number of Dockerfiles for building a variety of
fuzzing targets.

## Usage

First compile the base Docker image:

```bash
docker build -t dataflow/base-image . --build-arg SSH_PRIVATE_KEY="$(cat ~/.ssh/id_rsa)"
```

Then build the image for the target you want to fuzz, e.g.:

```bash
docker build -t dataflow/bison bison/
```
