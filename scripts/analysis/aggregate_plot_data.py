#!/usr/bin/env python3

"""
Aggregate AFL plot data from repeated runs.

This will normalize timestamps and combine all the plot data into a single CSV
file.
"""

from argparse import ArgumentParser
from collections import defaultdict
import csv
from functools import partial
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

    # From all the AFL plot_data files, constract a dictionary with the
    # following format:
    #
    # {
    #   time_0: {
    #     'cycles_done': [ v_1, v_2, ..., v_n],
    #     'cur_path': [v_1, v_2, ..., v_n],
    #     ...
    #   },
    #   time_1: {
    #     'cycles_done': [ v_1, v_2, ..., v_n],
    #     'cur_path': [v_1, v_2, ..., v_n],
    #     ...
    #   },
    #   ...
    # }
    #
    # The dictionary is keyed with a normalized time value (current time - start
    # time). At each time sample, the dictionary contains the AFL plot data for
    # all `n` runs. Because AFL may sample plot data at different intervals
    # across runs, a value may be `None`. This gets fixed later

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
                # Normalize time
                unix_time = int(row['unix_time'])
                if not start_time:
                    start_time = unix_time
                time = unix_time - start_time

                for field in PLOT_DATA_FIELDS[1:]:
                    if field == 'map_size':
                        value = float(row['map_size'].split('%')[0])
                    elif field == 'execs_per_sec':
                        value = float(row['execs_per_sec'])
                    else:
                        value = int(row[field])

                    plot_data[time][field][i] = value

    # Create a dictionary that maps AFL plot data field names (e.g.,
    # cycles_done, unique_crashes, etc.) to a function that generates a column
    # name for a particular run.
    #
    # E.g.,
    #
    # {
    #   'cycles_done': lambda i: 'cycles_done_%d' % i,
    #   'unique_crashes': lambda i: 'unique_crashes_%d' % i,
    #   ...
    # }
    gen_plot_data_key = lambda s, i: '%s_%d' % (s, i)
    plot_data_keys = {s: partial(gen_plot_data_key, s) for s in
                      PLOT_DATA_FIELDS[1:]}

    num_campaigns = len(plot_data_paths)
    columns = ['time'] + \
              [plot_data_key(i) for i in range(0, num_campaigns)
               for plot_data_key in plot_data_keys.values()]

    # Aggregate all the AFL plot data into a single Pandas' data frame. Values
    # that are `None` (because of AFL's sampling rate) are forward filled. The
    # data frame has the following format:
    #
    # time cycles_done_0 ... unique_crashes_0 ... cycles_done_n ...
    #    0 ...
    #    1 ...
    #
    # Where `n` is the number of AFL runs
    aggregated_plot_data = defaultdict(list)
    for time, data in sorted(plot_data.items()):
        aggregated_plot_data['time'].append(time)

        for i in range(0, num_campaigns):
            for field in PLOT_DATA_FIELDS[1:]:
                value = data[field].get(i)
                aggregated_plot_data[plot_data_keys[field](i)].append(value)
    df = pd.DataFrame(data=aggregated_plot_data).ffill()

    # Save the data frame to a CSV file
    with open(args.output, 'w') as outf:
        df.to_csv(outf, index=False)


if __name__ == '__main__':
    main()
