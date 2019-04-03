#!/usr/bin/env python
#
# Produce statistics from dataflow fuzzing logs
#


from __future__ import print_function

from collections import defaultdict, namedtuple
import os
import re
import sys

from tabulate import tabulate


PTR_DEREF_RE = re.compile(r'__ptr_deref: accessing pool (0x[0-9a-f]+) \(allocation site (0x[0-9a-f]+)\) from (0x[0-9a-f]+)')


PtrDeref = namedtuple('PtrDeref', ['pool_id', 'tag', 'ret_addr'])


def main(args):
    if len(args) < 2:
        print('usage: %s /path/to/log' % args[0])
        return 1

    if not os.path.isfile(args[1]):
        print('error: %s is not a valid log file' % args[1])
        return 1

    ptr_deref_counts = defaultdict(int)

    #
    # Collect the data
    #

    with open(args[1], 'r') as infile:
        for line in infile:
            match = PTR_DEREF_RE.search(line)
            if match:
                pool_id = int(match.group(1), 16)
                tag = int(match.group(2), 16)
                ret_addr = int(match.group(3), 16)

                ptr_deref_counts[PtrDeref(pool_id, tag, ret_addr)] += 1

    #
    # Print the results
    #

    ptr_deref_table = [(ptr.pool_id, ptr.tag, ptr.ret_addr, count) for
                       ptr, count in ptr_deref_counts.items()]

    print('%s pointer dereferences\n' % args[1])
    print(tabulate(sorted(ptr_deref_table, key=lambda x: x[0]),
                   headers=['Pool ID', 'Tag', 'Ret. Addr.', 'Count'],
                   tablefmt='psql'))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
