#!/usr/bin/env python
#
# Wrapper around the fuzzer-test-suite build.sh script that will collect
# instrumentation stats as the build executes
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os
import re
import sys

from sh import Command
from tabulate import tabulate


FUZZALLOC_DEREF_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfInstrumentedDereferences')
FUZZALLOC_PROM_ALLOCA_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfAllocaArrayPromotion')
FUZZALLOC_PROM_GLOBAL_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfGlobalVariableArrayPromotion')
FUZZALLOC_TAG_DIRECT_CALLS_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfTaggedDirectCalls')
FUZZALLOC_TAG_INDIRECT_CALLS_RE = re.compile(r'\[([a-zA-Z0-9./_-]+)\] (\d+) NumOfTaggedIndirectCalls')


class ModuleStats(object):
    def __init__(self):
        self._derefs = 0
        self._alloca_proms = 0
        self._global_proms = 0
        self._tagged_direct_calls = 0
        self._tagged_indirect_calls = 0

    @property
    def derefs(self):
        return self._derefs

    @derefs.setter
    def derefs(self, count):
        self._derefs = count

    @property
    def alloca_proms(self):
        return self._alloca_proms

    @alloca_proms.setter
    def alloca_proms(self, count):
        self._alloca_proms = count

    @property
    def global_proms(self):
        return self._global_proms

    @global_proms.setter
    def global_proms(self, count):
        self._global_proms = count

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


inst_stats = defaultdict(ModuleStats)


def process_output(line):
    global inst_stats

    print(line, end='')

    match = FUZZALLOC_DEREF_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].derefs = count
        return

    match = FUZZALLOC_PROM_ALLOCA_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].alloca_proms = count
        return

    match = FUZZALLOC_PROM_GLOBAL_RE.search(line)
    if match:
        module = match.group(1)
        count = int(match.group(2))
        inst_stats[module].global_proms = count
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


def parse_args():
    parser = ArgumentParser(description='Wrapper around fuzzer-test-suite\'s '
                                        'build.sh that collects fuzzalloc '
                                        'instrumentation stats')
    parser.add_argument('--csv', help='Path to an output CSV file')
    parser.add_argument('build_path',
                        help='Path to the fuzzer-test-suite\'s build.sh')
    parser.add_argument('build_args', nargs='*',
                        help='Options to build.sh')

    return parser.parse_args()


def main():
    global inst_stats

    args = parse_args()

    build_path = args.build_path
    if not os.path.isfile(build_path):
        raise Exception('%s is not a valid build script' % build_path)

    #
    # Set the relevant environment variables. Note that we must disable parallel
    # builds otherwise tracking of modules will fail
    #

    env_vars = os.environ.copy()
    env_vars['FUZZING_ENGINE'] = 'datAFLow'
    env_vars['JOBS'] = '1'

    #
    # Do the build
    #

    build_cmd = Command(build_path).bake(*args.build_args, _env=env_vars)
    build_cmd(_out=process_output, _err_to_out=True)

    #
    # Print the results
    #

    stats_table = [(mod, stats.derefs, stats.alloca_proms, stats.global_proms,
                    stats.tagged_direct_calls) for mod, stats in
                   inst_stats.items()]

    print('\ninstrumentation stats\n')
    print(tabulate(sorted(stats_table, key=lambda x: x[0]),
                   headers=['Module', 'Derefs', 'Alloca promotions',
                            'Global promotions', 'Tagged Functions'],
                   tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['module', 'derefs', 'alloca promotions',
                                 'global promotions', 'tagged functions'])
            csv_writer.writerows(stats_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
