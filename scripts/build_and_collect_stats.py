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


CC_RE = re.compile(r'^  CC +(.+\..+)$')
FUZZALLOC_DEREF_RE = re.compile(r'(\d+) fuzzalloc-instrument-derefs +- Number of pointer dereferences instrumented')
FUZZALLOC_PROM_ALLOCA_RE = re.compile(r'(\d+) fuzzalloc-prom-static-arrays +- Number of alloca array promotions')
FUZZALLOC_PROM_GLOBAL_RE = re.compile(r'(\d+) fuzzalloc-prom-static-arrays +- Number of global variable array promotions')
FUZZALLOC_TAG_ALLOCS_RE = re.compile(r'(\d+) fuzzalloc-tag-dyn-allocs +- Number of tagged dynamic memory allocation function calls')


class ModuleStats(object):
    def __init__(self):
        self._derefs = 0
        self._alloca_proms = 0
        self._global_proms = 0
        self._tagged_funcs = 0

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
    def tagged_funcs(self):
        return self._tagged_funcs

    @tagged_funcs.setter
    def tagged_funcs(self, count):
        self._tagged_funcs = count


current_module = None
inst_stats = defaultdict(ModuleStats)


def process_output(line):
    global current_module
    global inst_stats

    print(line, end='')

    match = CC_RE.match(line)
    if match:
        current_module = match.group(1)
        return

    match = FUZZALLOC_DEREF_RE.search(line)
    if match:
        assert current_module, 'A compilation module should be defined'
        count = int(match.group(1))
        inst_stats[current_module].derefs = count
        return

    match = FUZZALLOC_PROM_ALLOCA_RE.search(line)
    if match:
        assert current_module, 'A compilation module should be defined'
        count = int(match.group(1))
        inst_stats[current_module].alloca_proms = count
        return

    match = FUZZALLOC_PROM_GLOBAL_RE.search(line)
    if match:
        assert current_module, 'A compilation module should be defined'
        count = int(match.group(1))
        inst_stats[current_module].global_proms = count
        return

    match = FUZZALLOC_TAG_ALLOCS_RE.search(line)
    if match:
        assert current_module, 'A compilation module should be defined'
        count = int(match.group(1))
        inst_stats[current_module].tagged_funcs = count
        return


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
    env_vars['FUZZALLOC_STATS'] = '1'
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
                    stats.tagged_funcs) for mod, stats in inst_stats.items()]

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
