#!/usr/bin/env python3

"""
Aggregate AFL plot data from repeated runs.
"""

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os


PLOT_DATA_FIELDS = ('unix_time', 'cycles_done', 'cur_path', 'paths_total',
                    'pending_total', 'pending_favs', 'map_size',
                    'unique_crashes', 'unique_hangs', 'max_depth',
                    'execs_per_sec')
OUT_PLOT_DATA_FIELDS = ('time',
                        'cycles_done_min', 'map_size_min', 'unique_crashes_min',
                        'cycles_done_avg', 'map_size_avg', 'unique_crashes_avg',
                        'cycles_done_max', 'map_size_max', 'unique_crashes_max')


def parse_args():
    """Parse command-line arguments."""
    parser = ArgumentParser(description='Aggregate AFL plot data from '
                                        'repeated runns')
    parser.add_argument('-o', '--output', required=True,
                        help='Path to the output CSV file.')
    parser.add_argument('output_dir', nargs='+',
                        help='AFL output directories to aggregate')

    return parser.parse_args()


def main():
    """The main function."""
    args = parse_args()

    plot_data_paths = set()
    for output_dir in args.output_dir:
        plot_data = os.path.join(output_dir, 'plot_data')
        if not os.path.isfile(plot_data):
            raise Exception('%s is not a valid AFL output directory' %
                            output_dir)
        plot_data_paths.add(plot_data)

    num_campaigns = len(plot_data_paths)
    csv.register_dialect('afl_plot_data', delimiter=',', skipinitialspace=True)
    results = defaultdict(lambda: defaultdict(dict))

    for i, plot_data_path in enumerate(plot_data_paths):
        with open(plot_data_path, 'r') as plot_data_file:
            reader = csv.DictReader(plot_data_file,
                                    fieldnames=PLOT_DATA_FIELDS,
                                    dialect='afl_plot_data')
            # Skip the header
            next(reader)

            start_time = None
            for row in reader:
                unix_time = int(row['unix_time'])
                if not start_time:
                    start_time = unix_time

                time = unix_time - start_time
                cycles_done = int(row['cycles_done'])
                map_size = float(row['map_size'].split('%')[0])
                unique_crashes = int(row['unique_crashes'])

                results[time]['cycles_done'][i] = cycles_done
                results[time]['map_size'][i] = map_size
                results[time]['unique_crashes'][i] = unique_crashes

    with open(args.output, 'w') as outf:
        cycles_done = lambda i: 'cycles_done_%d' % i
        map_size = lambda i: 'map_size_%d' % i
        unique_crashes = lambda i: 'unique_crashes_%d' % i

        fieldnames = ['time'] + \
                     [cycles_done(i) for i in range(0, num_campaigns)] + \
                     [map_size(i) for i in range(0, num_campaigns)] + \
                     [unique_crashes(i) for i in range(0, num_campaigns)]

        writer = csv.DictWriter(outf, fieldnames=fieldnames)
        writer.writeheader()

        for time, data in sorted(results.items()):
            d = dict(time=time)
            for i in range(0, num_campaigns):
                d[cycles_done(i)] = data['cycles_done'].get(i, '')
                d[map_size(i)] = data['map_size'].get(i, '')
                d[unique_crashes(i)] = data['unique_crashes'].get(i, '')

            writer.writerow(d)


if __name__ == '__main__':
    main()
