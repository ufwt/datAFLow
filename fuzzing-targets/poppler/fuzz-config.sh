#!/bin/bash

AFL_TARGET=poppler-afl/bin/pdftotext
AFL_LD=poppler-afl/bin/pdftotext_deps

DATAFLOW_ACCESS_TARGET=poppler-datAFLow-access/bin/pdftotext
DATAFLOW_ACCESS_LD=poppler-datAFLow-access/bin/pdftotext_prelink

DATAFLOW_ACCESS_IDX_TARGET=poppler-datAFLow-access-idx/bin/pdftotext
DATAFLOW_ACCESS_IDX_LD=poppler-datAFLow-access-idx/bin/pdftotext_prelink

TARGET_OPTS="@@ /dev/null"
