#!/bin/bash

AFL_TARGET=${WORKDIR}/poppler-afl/bin/pdftotext

DATAFLOW_ACCESS_TARGET=${WORKDIR}/poppler-datAFLow-access/bin/pdftotext
DATAFLOW_ACCESS_LD=${WORKDIR}/poppler-datAFLow-access/bin/pdftotext_prelink

DATAFLOW_ACCESS_IDX_TARGET=${WORKDIR}/poppler-datAFLow-access-idx/bin/pdftotext
DATAFLOW_ACCESS_IDX_LD=${WORKDIR}/poppler-datAFLow-access-idx/bin/pdftotext_prelink

TARGET_OPTS="@@ /dev/null"
