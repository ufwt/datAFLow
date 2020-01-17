#!/bin/bash

AFL_TARGET=${WORKDIR}/bison-afl/bin/bison

DATAFLOW_ACCESS_TARGET=${WORKDIR}/bison-datAFLow-access/bin/bison
DATAFLOW_ACCESS_LD=${WORKDIR}/bison-datAFLow-access/bin/bison_prelink

DATAFLOW_ACCESS_IDX_TARGET=${WORKDIR}/bison-datAFLow-access-idx/bin/bison
DATAFLOW_ACCESS_IDX_LD=${WORKDIR}/bison-datAFLow-access-idx/bin/bison_prelink

TARGET_OPTS="-o /dev/null @@"
