#!/usr/bin/env python3


import os
import sys

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib import rc


rc('font', **{'family': 'serif', 'serif': ['Palatino']})
rc('text', usetex=True)


FUZZERS = {'afl': 'AFL',
        'datAFLow-access': 'datAFLow (access)',
        'datAFLow-access-idx': 'datAFLow (access + index)',
        'angora-track': 'Angora',
        }


THIS_DIR = os.path.dirname(__file__)


def main():
    if len(sys.argv) < 2:
        raise Exception('usage: %s /path/to/output/pdf' % sys.argv[0])

    df = pd.read_csv(os.path.join(THIS_DIR, 'results', 'results.csv'))
    fuzzer_cols = list(FUZZERS.keys())
    df[fuzzer_cols] = df[fuzzer_cols].div(df.clang, axis=0)
    df = df.drop('clang', axis=1)

    num_targets, num_fuzzers = df.shape
    index = np.arange(num_targets)
    bar_width = 0.8 / num_fuzzers

    # Plot

    plt.style.use('ggplot')

    for i, (fuzzer, label) in enumerate(FUZZERS.items()):
        plt.bar(index + bar_width * i, df[fuzzer], width=bar_width,
                label=label, log=True)

    plt.xlabel('Benchmark')
    plt.ylabel('Overhead (×)')
    plt.legend(loc='upper center', bbox_to_anchor=(0.5, 1.15), ncol=num_fuzzers)
    plt.xticks(index + bar_width, df.target, rotation=90)
    plt.ylim(bottom=1)

    print('Mean overhead')
    print(df.mean(skipna=None))

    plt.savefig(sys.argv[1], bbox_inches="tight")


if __name__ == '__main__':
    main()
