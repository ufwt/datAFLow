#!/usr/bin/env python
#
# Replay the AFL queue and collect the fuzzalloc logs
#


from __future__ import print_function

import os
import re
import subprocess
import sys


FUZZER_STATS_CMD_LINE_RE = re.compile(r'^command_line +: .+ -- (.+)')


def main(args):
    prog = args.pop(0)
    if len(args) < 2:
        print('usage: %s /path/to/afl/output/dir /path/to/logs/dir' % prog)
        return 1

    afl_out_dir = args.pop(0)
    fuzzer_stats_path = os.path.join(afl_out_dir, 'fuzzer_stats')
    queue_path = os.path.join(afl_out_dir, 'queue')

    if not os.path.isfile(fuzzer_stats_path):
        raise Exception('%s does not contain fuzzer_stats' % afl_out_dir)
    if not os.path.isdir(queue_path):
        raise Exception('%s does not contain a queue directory' % afl_out_dir)

    logs_out_dir = args.pop(0)
    if not os.path.isdir(logs_out_dir):
        raise Exception('%s is not a valid output directory' % logs_out_dir)

    if 'LD_LIBRARY_PATH' not in os.environ:
        print('warn: LD_LIBRARY_PATH is not set. Are you sure that this is '
              'not needed?')

    #
    # Grab the command-line used to run AFL from fuzzer_stats
    #

    cmd_line = None

    with open(fuzzer_stats_path, 'r') as fuzzer_stats:
        for line in fuzzer_stats:
            match = FUZZER_STATS_CMD_LINE_RE.match(line)
            if not match:
                continue

            cmd_line = match.group(1)

    if not cmd_line:
        raise Exception('Failed to find `command_line` in fuzzer_stats')

    #
    # Replay all of the files in the queue
    #

    for f in os.listdir(queue_path):
        args = cmd_line.replace('@@', os.path.join(queue_path, f)).split()

        with open(os.path.join(logs_out_dir, '%s.log' % f), 'w') as log_file:
            print('replayig %s' % f)
            proc = subprocess.Popen(args, stderr=log_file)
            proc.wait()

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
