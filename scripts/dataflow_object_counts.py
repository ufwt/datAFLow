#!/usr/bin/env python
#
# Collect alloca and global variable counts from a build log
#

from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os
import re
import sys

from tabulate import tabulate


FUZZALLOC_ALLOCA_COUNT_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfAllocas')
FUZZALLOC_GLOBAL_VAR_COUNT_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfGlobalVars')


class ModuleObjectCounts(object):
    def __init__(self):
        self._allocas = 0
        self._global_vars = 0

    @property
    def allocas(self):
        return self._allocas

    @allocas.setter
    def allocas(self, count):
        self._allocas = count

    @property
    def global_vars(self):
        return self._global_vars

    @global_vars.setter
    def global_vars(self, count):
        self._global_vars = count


object_counts = defaultdict(ModuleObjectCounts)


def parse_line(line):
    global object_counts

    match = FUZZALLOC_ALLOCA_COUNT_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        object_counts[module].allocas = count
        return

    match = FUZZALLOC_GLOBAL_VAR_COUNT_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        object_counts[module].global_vars = count
        return


def parse_args():
    parser = ArgumentParser(description='Collect alloca and global variable '
                                        'counts from a build log')
    parser.add_argument('--csv', help='Path to an output CSV file')
    parser.add_argument('input', help='Path to the build log')

    return parser.parse_args()


def main():
    global object_counts

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

    stats_table = [(mod, stats.allocas, stats.global_vars)
                   for mod, stats in object_counts.items()]

    print('\nObject counts\n')
    print(tabulate(sorted(stats_table, key=lambda x: x[0]) +
                   [('Total',
                     sum(stats.allocas for stats in object_counts.values()),
                     sum(stats.global_vars for stats in object_counts.values()))],
                   headers=['Module', 'Allocas', 'Global variables'],
                   tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['module', 'allocas', 'global variables',])
            csv_writer.writerows(stats_table)

    return 0

if __name__ == '__main__':
    sys.exit(main())
