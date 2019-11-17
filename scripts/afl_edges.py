#!/usr/bin/env python
#
# Produce statistics from AFL (debug) logs
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict, namedtuple
import csv
import os
import re
import sys

from tabulate import tabulate


# Regex for pulling out control flow edge coverage from an AFL log file
AFL_EDGE_RE = re.compile(r'prev loc ((?:0x)?[0-9a-f]+), cur loc ((?:0x)?[0-9a-f]+)')

Edge = namedtuple('Edge', ['prev_loc', 'cur_loc'])


def parse_args():
    parser = ArgumentParser(description='Statistics from AFL fuzzing logs')
    parser.add_argument('-s', '--silent', action='store_true',
                        help='Don\'t print the results to stdout')
    parser.add_argument('--csv', help='Path to an output CSV file')
    parser.add_argument('log_path', help='Path to an AFL log file')

    return parser.parse_args()


def main():
    args = parse_args()

    log_path = args.log_path
    if not os.path.isfile(log_path):
        raise Exception('%s is not a valid log file' % log_path)

    edge_counts = defaultdict(int)

    #
    # Collect the data
    #

    with open(log_path, 'r') as infile:
        for line in infile:
            match = AFL_EDGE_RE.search(line)
            if match:
                prev_loc = int(match.group(1), 16)
                cur_loc = int(match.group(2), 16)

                edge_counts[Edge(prev_loc, cur_loc)] += 1

    #
    # Print/save the results
    #

    edge_table = [(edge.prev_loc, edge.cur_loc, count) for edge, count
                  in edge_counts.items()]

    if not args.silent:
        print('Edges\n')
        print(tabulate(sorted(edge_table, key=lambda x: x[0]),
                       headers=['Prev loc', 'Cur loc', 'Count'],
                       tablefmt='psql'))

    csv_path = args.csv
    if csv_path:
        with open(csv_path, 'w') as csvfile:
            csv_writer = csv.writer(csvfile)

            csv_writer.writerow(['prev loc', 'cur loc', 'count'])
            csv_writer.writerows(edge_table)

    return 0


if __name__ == '__main__':
    sys.exit(main())
