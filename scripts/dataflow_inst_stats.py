#!/usr/bin/env python
#
# Collect instrumentation stats from a datAFLow-instrumented build log
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os
import re
import sys

from tabulate import tabulate


FUZZALLOC_MEM_ACCESSES_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfInstrumentedMemAccesses')
FUZZALLOC_HEAPIFY_ALLOCA_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfAllocaArrayHeapification')
FUZZALLOC_HEAPIFY_GLOBAL_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfGlobalVariableArrayHeapification')
FUZZALLOC_TAG_DIRECT_CALLS_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfTaggedDirectCalls')
FUZZALLOC_TAG_INDIRECT_CALLS_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfTaggedIndirectCalls')
FUZZALLOC_NEW_REWRITES_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfNewRewrites')
FUZZALLOC_DELETE_REWRITES_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfDeleteRewrites')


class ModuleStats(object):
    def __init__(self):
        self._mem_accesses = 0
        self._alloca_heapifys = 0
        self._global_heapifys = 0
        self._tagged_direct_calls = 0
        self._tagged_indirect_calls = 0
        self._new_rewrites = 0
        self._delete_rewrites = 0

    @property
    def mem_accesses(self):
        return self._mem_accesses

    @mem_accesses.setter
    def mem_accesses(self, count):
        self._mem_accesses = count

    @property
    def alloca_heapifys(self):
        return self._alloca_heapifys

    @alloca_heapifys.setter
    def alloca_heapifys(self, count):
        self._alloca_heapifys = count

    @property
    def global_heapifys(self):
        return self._global_heapifys

    @global_heapifys.setter
    def global_heapifys(self, count):
        self._global_heapifys = count

    @property
    def tagged_direct_calls(self):
        return self._tagged_direct_calls

    @tagged_direct_calls.setter
    def tagged_direct_calls(self, count):
        self._tagged_direct_calls = count

    @property
    def tagged_indirect_calls(self):
        return self._tagged_indirect_calls

    @tagged_indirect_calls.setter
    def tagged_indirect_calls(self, count):
        self._tagged_indirect_calls = count

    @property
    def new_rewrites(self):
        return self._new_rewrites

    @new_rewrites.setter
    def new_rewrites(self, count):
        self._new_rewrites = count

    @property
    def delete_rewrites(self):
        return self._delete_rewrites

    @delete_rewrites.setter
    def delete_rewrites(self, count):
        self._delete_rewrites = count


inst_stats = defaultdict(ModuleStats)


def parse_line(line):
    global inst_stats

    match = FUZZALLOC_MEM_ACCESSES_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].mem_accesses = count
        return

    match = FUZZALLOC_HEAPIFY_ALLOCA_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].alloca_heapifys = count
        return

    match = FUZZALLOC_HEAPIFY_GLOBAL_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].global_heapifys = count
        return

    match = FUZZALLOC_TAG_DIRECT_CALLS_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].tagged_direct_calls = count
        return

    match = FUZZALLOC_TAG_INDIRECT_CALLS_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].tagged_indirect_calls = count
        return

    match = FUZZALLOC_NEW_REWRITES_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].new_rewrites = count
        return

    match = FUZZALLOC_DELETE_REWRITES_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].delete_rewrites = count
        return


def parse_args():
    parser = ArgumentParser(description='Collect datAFLow instrumentation '
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

    stats_table = [(mod, stats.new_rewrites, stats.delete_rewrites,
                    stats.alloca_heapifys, stats.global_heapifys,
                    stats.tagged_direct_calls, stats.tagged_indirect_calls,
                    stats.mem_accesses) for mod, stats in inst_stats.items()]

    print('\ndatAFLow instrumentation stats\n')
    print(tabulate(sorted(stats_table, key=lambda x: x[0]) +
                   [('Total',
                     sum(stats.new_rewrites for stats in inst_stats.values()),
                     sum(stats.delete_rewrites for stats in inst_stats.values()),
                     sum(stats.alloca_heapifys for stats in inst_stats.values()),
                     sum(stats.global_heapifys for stats in inst_stats.values()),
                     sum(stats.tagged_direct_calls for stats in inst_stats.values()),
                     sum(stats.tagged_indirect_calls for stats in inst_stats.values()),
                     sum(stats.mem_accesses for stats in inst_stats.values()))],
                   headers=['Module', 'New rewrites', 'Delete rewrites',
                            'Alloca heapifications', 'Global heapifications',
                            'Tagged direct calls', 'Tagged indirect calls',
                            'Memory accesses'],
                   tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['module', 'new rewrites', 'delete rewrites',
                                 'alloca heapifications',
                                 'global heapifications', 'tagged direct calls',
                                 'tagged indirect calls', 'mem accesses'])
            csv_writer.writerows(stats_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
