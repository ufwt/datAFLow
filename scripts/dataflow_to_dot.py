#!/usr/bin/env python
#
# Convert fuzzalloc dataflow log to a graphviz dot file
#


from __future__ import print_function

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


POOL_ACCESS_RE = re.compile(r'accessing pool 0x[0-9a-f]+ \(allocation site (0x[0-9a-f]+)\) from (0x[0-9a-f]+)')


def main(args):
    prog = args.pop(0)
    if len(args) < 2:
        print('usage: %s /path/to/log /path/to/dot' % prog)
        return 1

    log_path = args.pop(0)
    if not os.path.isfile(log_path):
        raise Exception('%s is not a valid log file' % log_path)

    use_counts = defaultdict(int)

    #
    # Get the data
    #

    with open(log_path, 'r') as infile:
        for line in infile:
            match = POOL_ACCESS_RE.search(line)
            if not match:
                continue

            alloc_site = int(match.group(1), 16)
            use_site = int(match.group(2), 16)

            use_counts[(alloc_site, use_site)] += 1

    #
    # Generate a graph
    #

    dot_path = args.pop(0)
    graph = nx.DiGraph()
    graph.add_weighted_edges_from([(alloc, use, count) for (alloc, use), count
                                   in use_counts.items()])
    write_dot(graph, dot_path)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
