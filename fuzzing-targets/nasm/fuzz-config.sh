#!/bin/bash

AFL_TARGET=nasm-afl/bin/nasm
AFL_LD=nasm-afl/bin/nasm_deps

DATAFLOW_ACCESS_TARGET=nasm-datAFLow-access/bin/nasm
DATAFLOW_ACCESS_LD=nasm-datAFLow-access/bin/nasm_prelink

DATAFLOW_ACCESS_IDX_TARGET=nasm-datAFLow-access-idx/bin/nasm
DATAFLOW_ACCESS_IDX_LD=nasm-datAFLow-access-idx/bin/nasm_prelink

TARGET_OPTS="-o dev/null -- @@"
