#!/usr/bin/env python
#
# Collect instrumentation stats from an AFL-instrumented build log
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os
import re
import sys

from tabulate import tabulate


AFL_BASIC_BLOCK_INST_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] Instrumented (\d+) locations')


inst_stats = defaultdict(int)


def parse_line(line):
    global inst_stats

    match = AFL_BASIC_BLOCK_INST_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module] = count
        return


def parse_args():
    parser = ArgumentParser(description='Collect AFL instrumentation '
                                        'statistics from a build log')
    parser.add_argument('--csv', help='Path to an output CSV file')
    parser.add_argument('input', help='Path to the build log')

    return parser.parse_args()


def main():
    global inst_stats

    args = parse_args()

    build_log_path = args.input
    if not os.path.isfile(build_log_path):
        raise Exception('%s is not a valid build log' % build_log_path)

    #
    # Parse the build log
    #

    with open(build_log_path, 'r') as build_log:
        for line in build_log:
            parse_line(line)

    #
    # Print the results
    #

    stats_table = [(mod, basic_blocks) for mod, basic_blocks in
                   inst_stats.items()]

    print('\nAFL instrumentation stats\n')
    print(tabulate(sorted(stats_table, key=lambda x: x[0]) +
                   [('Total', sum(inst_stats.values()))],
                   headers=['Module', 'Basic blocks'], tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['module', 'basic blocks'])
            csv_writer.writerows(stats_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
