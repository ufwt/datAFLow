#!/usr/bin/env python
#
# Replay the AFL queue in afl-showmap
#


from __future__ import print_function

from argparse import ArgumentParser
import multiprocessing
import os
from subprocess import Popen
import sys

from common import get_afl_command_line


def parse_args():
    parser = ArgumentParser(description='Replay the AFL queue in afl-showmap')
    parser.add_argument('-o', '--output', required=True,
                        help='Output directory path')
    parser.add_argument('-a', '--afl-path', required=True,
                        help='AFL build directory path')
    parser.add_argument('afl_out_dir', help='AFL output directory')

    return parser.parse_args()


def showmap(afl_cmd_line, afl_showmap_path, input_path, output_path):
    if '@@' in afl_cmd_line.target_cmd_line:
        args = afl_cmd_line.target_cmd_line.replace('@@', input_path).split()
    else:
        args = afl_cmd_line.target_cmd_line.split() + [input_path]

    afl_showmap_args = [afl_showmap_path, '-q', '-o', output_path]
    if afl_cmd_line.timeout:
        afl_showmap_args.extend(['-t', afl_cmd_line.timeout])
    if afl_cmd_line.memory_limit:
        afl_showmap_args.extend(['-m', afl_cmd_line.memory_limit])

    with open(os.devnull, 'w') as devnull:
        proc = Popen(afl_showmap_args + ['--'] + args, stderr=devnull)
        proc.wait()
        print('`afl-showmap %s` returned %d' % (input_path, proc.returncode))


def main():
    args = parse_args()

    afl_out_dir = args.afl_out_dir
    fuzzer_stats_path = os.path.join(afl_out_dir, 'fuzzer_stats')
    queue_path = os.path.join(afl_out_dir, 'queue')

    if not os.path.isfile(fuzzer_stats_path):
        raise Exception('%s does not contain fuzzer_stats' % afl_out_dir)
    if not os.path.isdir(queue_path):
        raise Exception('%s does not contain a queue directory' % afl_out_dir)

    afl_path = args.afl_path
    afl_showmap_path = os.path.join(afl_path, 'afl-showmap')
    if not os.path.isfile(afl_showmap_path):
        raise Exception('%s does not contain afl-showmap' % afl_path)

    maps_out_dir = args.output
    if not os.path.isdir(maps_out_dir):
        raise Exception('%s is not a valid output directory' % maps_out_dir)

    if 'LD_LIBRARY_PATH' not in os.environ:
        print('warn: LD_LIBRARY_PATH is not set. Are you sure that this is not '
              'needed?')

    #
    # Grab the command-line used to run AFL from fuzzer_stats
    #

    afl_cmd_line = get_afl_command_line(fuzzer_stats_path)
    print('replaying AFL target `%s`' % afl_cmd_line.target_cmd_line)

    #
    # Run afl-showmap on all of the files in the queue
    #

    pool = multiprocessing.Pool(multiprocessing.cpu_count() // 2)
    for f in os.listdir(queue_path):
        input_path = os.path.join(queue_path, f)
        output_path = os.path.join(maps_out_dir, '%s.map' % f)

        pool.apply_async(showmap, args=(afl_cmd_line, afl_showmap_path,
                                        input_path, output_path))
    pool.close()
    pool.join()

    return 0


if __name__ == '__main__':
    sys.exit(main())
