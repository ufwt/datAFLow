# SPEC CPU2006

This set of files allows you to benchmark datAFLow instrumentation on the [SPEC
CPU2006](https://www.spec.org/cpu2006/) suite.

## Requirements

You'll need to install `prelink` and `patchlelf` to be able to shrink the
address space (this occurs automatically via the `wrap_prelink.sh` script).

## Usage

Run `setup.sh` in your SPEC CPU2006 installation directory. Then run the
benchmark with the appropriate config file.

Note that the `-debug.cfg` config files are intended for debugging the LLVM
bitcode and should **not** be used for benchmarking purposes.
