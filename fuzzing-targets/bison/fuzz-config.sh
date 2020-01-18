#!/bin/bash

AFL_TARGET=bison-afl/bin/bison

DATAFLOW_ACCESS_TARGET=bison-datAFLow-access/bin/bison
DATAFLOW_ACCESS_LD=bison-datAFLow-access/bin/bison_prelink

DATAFLOW_ACCESS_IDX_TARGET=bison-datAFLow-access-idx/bin/bison
DATAFLOW_ACCESS_IDX_LD=bison-datAFLow-access-idx/bin/bison_prelink

TARGET_OPTS="-o /dev/null @@"
