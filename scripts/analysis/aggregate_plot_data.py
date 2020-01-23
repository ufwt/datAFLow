#!/usr/bin/env python3

"""
Aggregate AFL plot data from repeated runs.

This will normalize timestamps and combine all the plot data into a single CSV
file.
"""

from argparse import ArgumentParser
from collections import defaultdict
import csv
import os

import pandas as pd


PLOT_DATA_FIELDS = ('unix_time', 'cycles_done', 'cur_path', 'paths_total',
                    'pending_total', 'pending_favs', 'map_size',
                    'unique_crashes', 'unique_hangs', 'max_depth',
                    'execs_per_sec')


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

    csv.register_dialect('afl_plot_data', delimiter=',', skipinitialspace=True)
    plot_data = defaultdict(lambda: defaultdict(dict))

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
                cur_path = int(row['cur_path'])
                paths_total = int(row['paths_total'])
                pending_total = int(row['pending_total'])
                pending_favs = int(row['pending_favs'])
                map_size = float(row['map_size'].split('%')[0])
                unique_crashes = int(row['unique_crashes'])
                unique_hangs = int(row['unique_hangs'])
                max_depth = int(row['max_depth'])
                execs_per_second = float(row['execs_per_sec'])

                plot_data[time]['cycles_done'][i] = cycles_done
                plot_data[time]['cur_path'][i] = cur_path
                plot_data[time]['paths_total'][i] = paths_total
                plot_data[time]['pending_total'][i] = pending_total
                plot_data[time]['pending_favs'][i] = pending_favs
                plot_data[time]['map_size'][i] = map_size
                plot_data[time]['unique_crashes'][i] = unique_crashes
                plot_data[time]['unique_hangs'][i] = unique_hangs
                plot_data[time]['max_depth'][i] = max_depth
                plot_data[time]['execs_per_sec'][i] = execs_per_sec

    cycles_done_key = lambda i: 'cycles_done_%d' % i
    map_size_key = lambda i: 'map_size_%d' % i
    unique_crashes_key = lambda i: 'unique_crashes_%d' % i

    num_campaigns = len(plot_data_paths)
    columns = ['time'] + \
              [cycles_done_key(i) for i in range(0, num_campaigns)] + \
              [map_size_key(i) for i in range(0, num_campaigns)] + \
              [unique_crashes_key(i) for i in range(0, num_campaigns)]

    aggregated_plot_data = defaultdict(list)

    for time, data in sorted(plot_data.items()):
        aggregated_plot_data['time'].append(time)

        for i in range(0, num_campaigns):
            cycles_done = data['cycles_done'].get(i)
            map_size = data['map_size'].get(i)
            unique_crashes = data['unique_crashes'].get(i)

            aggregated_plot_data[cycles_done_key(i)].append(cycles_done)
            aggregated_plot_data[map_size_key(i)].append(map_size)
            aggregated_plot_data[unique_crashes_key(i)].append(unique_crashes)

    df = pd.DataFrame(data=aggregated_plot_data).ffill()
    with open(args.output, 'w') as outf:
        df.to_csv(outf, index=False)


if __name__ == '__main__':
    main()
