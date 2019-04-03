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
    prog = args.pop(0)
    if len(args) < 1:
        print('usage: %s /path/to/log' % prog)
        return 1

    log_path = args.pop(0)
    if not os.path.isfile(log_path):
        raise Exception('%s is not a valid log file' % log_path)

    ptr_deref_counts = defaultdict(int)

    #
    # Collect the data
    #

    with open(log_path, 'r') as infile:
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

    print('%s pointer dereferences\n' % log_path)
    print(tabulate(sorted(ptr_deref_table, key=lambda x: x[0]),
                   headers=['Pool ID', 'Tag', 'Ret. Addr.', 'Count'],
                   tablefmt='psql'))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
