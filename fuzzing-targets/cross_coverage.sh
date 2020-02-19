#!/bin/bash -eux
#
# Executes AFL queues through different instrumentation to produce cross
# control-flow / data-flow
#

if [ "$#" -ne 1 ]; then
    echo "usage: $0 TARGET"
    exit 1
fi

THIS_DIR="." #$(readlink -f $(dirname "${0}"))
TARGET=${1}

export PATH="$(pwd)/AFL:${PATH}"

#
# Aggregate raw AFL/datAFLow data
#

PLOT_DATA="${THIS_DIR}/${TARGET}/afl-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/afl.csv                      \
    ${PLOT_DATA}
PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-afl-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_afl.csv                 \
    ${PLOT_DATA}
PLOT_DATA="${THIS_DIR}/${TARGET}/datAFLow-access-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/dataflow_access.csv          \
    ${PLOT_DATA}
PLOT_DATA="${THIS_DIR}/${TARGET}/datAFLow-access-idx-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/dataflow_access_idx.csv      \
    ${PLOT_DATA}
PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-datAFLow-access-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_dataflow_access.csv     \
    ${PLOT_DATA}
PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-datAFLow-access-idx-out-*/plot_data"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_dataflow_access_idx.csv \
    ${PLOT_DATA}

#
# Augment plot_data with testcases
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps                 \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/afl-out-$I
done

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps                 \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/mopt-afl-out-$I
done

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET}_deps    \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/datAFLow-access-out-$I
done

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/datAFLow-access-idx-out-$I
done

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-out-$I
done

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/plot_data_testcase.py -i -b ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-idx-out-$I
done

#
# replay datAFLow-access through AFL (control-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps     \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/datAFLow-access-out-$I/afl_plot_data.csv  \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET} ${THIS_DIR}/${TARGET}/datAFLow-access-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/datAFLow-access-out-*/afl_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/dataflow_access_in_afl.csv   \
    ${PLOT_DATA}

#
# replay datAFLow-access-idx through AFL (control-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps         \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/datAFLow-access-idx-out-$I/afl_plot_data.csv  \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET} ${THIS_DIR}/${TARGET}/datAFLow-access-idx-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/datAFLow-access-idx-out-*/afl_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/dataflow_access_idx_in_afl.csv   \
    ${PLOT_DATA}

#
# replay MOpt-datAFLow-access through AFL (control-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps         \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-out-$I/afl_plot_data.csv \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET} ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-datAFLow-access-out-*/afl_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_dataflow_access_in_afl.csv  \
    ${PLOT_DATA}

#
# replay MOpt-datAFLow-access-idx through AFL (control-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET}_deps             \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-idx-out-$I/afl_plot_data.csv \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-afl/bin/${TARGET} ${THIS_DIR}/${TARGET}/mopt-datAFLow-access-idx-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-datAFLow-access-idx-out-*/afl_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_dataflow_access_idx_in_afl.csv  \
    ${PLOT_DATA}

#
# replay AFL through datAFLow-access (data-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/afl-out-$I/datAFLow_access_plot_data.csv          \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET} ${THIS_DIR}/${TARGET}/afl-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/afl-out-*/datAFLow_access_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/afl_in_dataflow_access.csv   \
    ${PLOT_DATA}

#
# replay AFL through datAFLow-access-idx (data-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/afl-out-$I/datAFLow_access_idx_plot_data.csv          \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET} ${THIS_DIR}/${TARGET}/afl-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/afl-out-*/datAFLow_access_idx_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/afl_in_dataflow_access_idx.csv   \
    ${PLOT_DATA}

#
# replay MOpt-AFL through datAFLow-access (data-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/mopt-afl-out-$I/datAFLow_access_plot_data.csv     \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access/bin/${TARGET} ${THIS_DIR}/${TARGET}/mopt-afl-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-afl-out-*/datAFLow_access_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_afl_in_dataflow_access.csv  \
    ${PLOT_DATA}

#
# replay MOpt-AFL through datAFLow-access-idx (data-flow)
#

for I in $(seq 1 5); do
  LD_LIBRARY_PATH=${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET}_deps \
  ${THIS_DIR}/fuzzing-data-analysis/afl_scripts/replay_queue.py -o ${THIS_DIR}/${TARGET}/mopt-afl-out-$I/datAFLow_access_idx_plot_data.csv     \
  -r -p ${THIS_DIR}/${TARGET}/${TARGET}-datAFLow-access-idx/bin/${TARGET} ${THIS_DIR}/${TARGET}/mopt-afl-out-$I
done

PLOT_DATA="${THIS_DIR}/${TARGET}/mopt-afl-out-*/datAFLow_access_idx_plot_data.csv"
${THIS_DIR}/fuzzing-data-analysis/afl_scripts/aggregate_plot_data.py -o ${THIS_DIR}/${TARGET}/mopt_afl_in_dataflow_access_idx.csv  \
    ${PLOT_DATA}
