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


PTR_DEREF_RE = re.compile(r'__ptr_deref: accessing pool (0x[0-9a-f]+) \(allocation site (0x[0-9a-f]+)\) from (0x[0-9a-f]+)')


PtrDeref = namedtuple('PtrDeref', ['pool_id', 'tag', 'ret_addr'])


def parse_args():
    parser = ArgumentParser(description='Statistics from datAFLow fuzzing logs')
    parser.add_argument('-i', '--ignore-pool-zero', action='store_true',
                        help='Ignore pool zero accesses')
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

    ptr_deref_counts = defaultdict(int)

    #
    # Collect the data
    #

    ignore_pool_zero = args.ignore_pool_zero

    with open(log_path, 'r') as infile:
        for line in infile:
            match = PTR_DEREF_RE.search(line)
            if match:
                pool_id = int(match.group(1), 16)
                tag = int(match.group(2), 16)
                ret_addr = int(match.group(3), 16)

                if ignore_pool_zero and pool_id == 0:
                    continue

                ptr_deref_counts[PtrDeref(pool_id, tag, ret_addr)] += 1

    #
    # Print the results
    #

    ptr_deref_table = [(ptr.pool_id, ptr.tag, ptr.ret_addr, count) for
                       ptr, count in ptr_deref_counts.items()]

    if not args.silent:
        print('pointer dereferences\n')
        print(tabulate(sorted(ptr_deref_table, key=lambda x: x[0]),
                       headers=['Pool ID', 'Tag', 'Ret. Addr.', 'Count'],
                       tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['pool id', 'tag', 'return address', 'count'])
            csv_writer.writerows(ptr_deref_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
