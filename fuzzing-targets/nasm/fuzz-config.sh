#!/bin/bash

AFL_TARGET=${WORKDIR}/nasm-afl/bin/nasm

DATAFLOW_ACCESS_TARGET=${WORKDIR}/nasm-datAFLow-access/bin/nasm
DATAFLOW_ACCESS_LD=${WORKDIR}/nasm-datAFLow-access/bin/nasm_prelink

DATAFLOW_ACCESS_IDX_TARGET=${WORKDIR}/nasm-datAFLow-access-idx/bin/nasm
DATAFLOW_ACCESS_IDX_LD=${WORKDIR}/nasm-datAFLow-access-idx/bin/nasm_prelink

TARGET_OPTS="-o dev/null -- @@"
