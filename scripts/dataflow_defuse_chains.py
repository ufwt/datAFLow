#!/usr/bin/env python
#
# Produce statistics from dataflow fuzzing logs
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict, namedtuple
import csv
import os
import re
import sys

from tabulate import tabulate


from common import FUZZALLOC_LOG_MEM_ACCESS_RE


MemAccess = namedtuple('MemAccess', ['def_site', 'use_site', 'offset'])


def parse_args():
    parser = ArgumentParser(description='Statistics from datAFLow fuzzing logs')
    parser.add_argument('-s', '--silent', action='store_true',
                        help='Don\'t print the results to stdout')
    parser.add_argument('--csv', help='Path to an output CSV file')
    parser.add_argument('log_path', help='Path to a libfuzzalloc log file')

    return parser.parse_args()


def main():
    args = parse_args()

    log_path = args.log_path
    if not os.path.isfile(log_path):
        raise Exception('%s is not a valid log file' % log_path)

    mem_access_counts = defaultdict(int)

    #
    # Collect the data
    #

    with open(log_path, 'r') as infile:
        for line in infile:
            match = FUZZALLOC_LOG_MEM_ACCESS_RE.search(line)
            if match:
                def_site = int(match.group(1), 16)
                use_site = int(match.group(2), 16)
                offset = int(match.group(3))

                mem_access_counts[MemAccess(def_site, use_site, offset)] += 1

    #
    # Print/save the results
    #

    mem_access_table = [(mem_access.def_site, mem_access.use_site,
                         mem_access.offset, count) for
                        mem_access, count in mem_access_counts.items()]

    if not args.silent:
        print('Def/use chains\n')
        print(tabulate(sorted(mem_access_table, key=lambda x: x[0]),
                       headers=['Def site', 'Use site', 'Offset', 'Count'],
                       tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['def site', 'use site', 'offset', 'count'])
            csv_writer.writerows(mem_access_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
