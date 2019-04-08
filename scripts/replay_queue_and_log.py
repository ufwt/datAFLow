#!/usr/bin/env python
#
# Replay the AFL queue and collect the fuzzalloc logs
#


from __future__ import print_function

from argparse import ArgumentParser
import os
import subprocess
import sys

from common import get_afl_command_line


def parse_args():
    parser = ArgumentParser(description='Replay the AFL queue and collect logs')
    parser.add_argument('-o', '--output', required=True,
                        help='Output directory path')
    parser.add_argument('afl_out_dir', help='AFL output directory')

    return parser.parse_args()


def main():
    args = parse_args()

    afl_out_dir = args.afl_out_dir
    fuzzer_stats_path = os.path.join(afl_out_dir, 'fuzzer_stats')
    queue_path = os.path.join(afl_out_dir, 'queue')

    if not os.path.isfile(fuzzer_stats_path):
        raise Exception('%s does not contain fuzzer_stats' % afl_out_dir)
    if not os.path.isdir(queue_path):
        raise Exception('%s does not contain a queue directory' % afl_out_dir)

    logs_out_dir = args.output
    if not os.path.isdir(logs_out_dir):
        raise Exception('%s is not a valid output directory' % logs_out_dir)

    if 'LD_LIBRARY_PATH' not in os.environ:
        print('warn: LD_LIBRARY_PATH is not set. Are you sure that this is not '
              'needed?')

    #
    # Grab the command-line used to run AFL from fuzzer_stats
    #

    afl_cmd_line = get_afl_command_line(fuzzer_stats_path)

    #
    # Replay all of the files in the queue
    #

    for f in os.listdir(queue_path):
        input_path = os.path.join(queue_path, f)
        args = afl_cmd_line.target_cmd_line.replace('@@', input_path).split()

        with open(os.path.join(logs_out_dir, '%s.log' % f), 'w') as log_file:
            proc = subprocess.Popen(args, stderr=log_file)
            proc.wait()
            print('replaying %s returned %d' % (f, proc.returncode))

    return 0


if __name__ == '__main__':
    sys.exit(main())
