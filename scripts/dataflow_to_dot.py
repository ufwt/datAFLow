#!/usr/bin/env python
#
# Convert fuzzalloc dataflow log to a graphviz dot file
#


from __future__ import print_function

from argparse import ArgumentParser
from collections import defaultdict
import os
import re
import sys

import networkx as nx
try:
    import pygraphiv
    from networkx.drawing.nx_agraph import write_dot
except ImportError:
    try:
        import pydot
        from networkx.drawing.nx_pydot import write_dot
    except ImportError:
        print('Both pygraphviz and pydot were not found')
        raise

from common import FUZZALLOC_LOG_MEM_ACCESS_RE


def parse_args():
    parser = ArgumentParser(description='Render datAFLow stats as DOT file')
    parser.add_argument('-i', '--ignore-pool-zero', action='store_true',
                        help='Ignore pool zero accesses')
    parser.add_argument('--dot', help='Path to an output DOT file')
    parser.add_argument('log_path', help='Path to a libfuzzalloc log file')

    return parser.parse_args()


def main():
    args = parse_args()

    log_path = args.log_path
    if not os.path.isfile(log_path):
        raise Exception('%s is not a valid log file' % log_path)

    use_counts = defaultdict(int)

    #
    # Get the data
    #

    ignore_pool_zero = args.ignore_pool_zero

    with open(log_path, 'r') as infile:
        for line in infile:
            match = FUZZALLOC_LOG_MEM_ACCESS_RE.search(line)
            if not match:
                continue

            pool_id = int(match.group(1), 16)
            tag = int(match.group(2), 16)
            ret_addr = int(match.group(3), 16)

            if ignore_pool_zero and pool_id == 0:
                continue

            use_counts[(tag, ret_addr)] += 1

    #
    # Generate a graph
    #

    graph = nx.DiGraph()
    graph.add_weighted_edges_from([(alloc, use, count) for (alloc, use), count
                                   in use_counts.items()])
    write_dot(graph, args.dot if args.dot else sys.stdout)

    return 0


if __name__ == '__main__':
    sys.exit(main())
