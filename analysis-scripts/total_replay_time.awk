#!/usr/bin/awk -f
#
# Sum the total perf time produced by the replay_collected_inputs.sh script

BEGIN { FS=" " }
$0 ~ /task-clock/ { TOTAL_TIME += $1 }
END { printf "Total time = %f ms\n", TOTAL_TIME }
