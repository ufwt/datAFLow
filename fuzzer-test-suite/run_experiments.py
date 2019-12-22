#!/usr/bin/env python3

"""
Run all AFL fuzzing campaigns.

Author: Adrian Herrera
"""


from argparse import ArgumentParser
from datetime import datetime
from enum import Enum
from itertools import product
import multiprocessing
import os
import subprocess


class SeedsLocation(Enum):
    """Describe the location of input seeds."""
    EMPTY = 1
    FUZZER_TEST_SUITE = 2
    RUNDIR = 3



TARGETS = {
    'boringssl-2016-02-12': {
        'seeds': SeedsLocation.RUNDIR,
    },
    'c-ares-CVE-2016-5180': {
        'seeds': SeedsLocation.EMPTY
    },
    'guetzli-2017-3-30': {
        'seeds': SeedsLocation.FUZZER_TEST_SUITE,
        'timeout': '2000+',
    },
    'harfbuzz-1.3.2': {
        'seeds': SeedsLocation.RUNDIR,
    },
    'json-2017-02-12': {
        'seeds': SeedsLocation.RUNDIR,
    },
    'lcms-2017-03-21': {
        'seeds': SeedsLocation.FUZZER_TEST_SUITE,
    },
    'libarchive-2017-01-04': {
        'seeds': SeedsLocation.FUZZER_TEST_SUITE,
    },
    'libxml2-v2.9.2': {
        'seeds': SeedsLocation.EMPTY,
    },
    'llvm-libcxxabi-2017-01-27': {
        'seeds': SeedsLocation.EMPTY,
    },
    'pcre2-10.00': {
        'seeds': SeedsLocation.EMPTY,
    },
    're2-2014-12-09': {
        'seeds': SeedsLocation.EMPTY,
    },
    'sqlite-2016-11-14': {
        'seeds': SeedsLocation.EMPTY,
    },
    'vorbis-2017-12-11': {
        'seeds': SeedsLocation.FUZZER_TEST_SUITE,
    },
    'woff2-2016-05-06': {
        'seeds': SeedsLocation.RUNDIR,
    },
}


def parse_args():
    """Parse command-line arguments."""
    parser = ArgumentParser(description='Run Google FTS experiments')
    parser.add_argument('-a', '--afl-dir', required=True, action='store',
                        help='Path to the AFL fuzzer directory')
    parser.add_argument('-p', '--output-prefix', required=False, action='store',
                        default='fuzz-out', help='Output directory prefix')
    parser.add_argument('-e', '--fuzzing-engine', required=True, action='store',
                        choices=('afl', 'datAFLow'), help='Fuzzing engine')
    parser.add_argument('-n', '--num-trials', required=False, default=5,
                        type=int, help='Number of repeated trials')
    parser.add_argument('-t', '--timeout', required=False, action='store',
                        default=24*60*60, type=int, help='Timeout (in seconds)')
    parser.add_argument('-f', '--fts', required=True, action='store',
                        help='Path to the Google FTS source directory')
    parser.add_argument('-o', '--afl-opt', required=False, action='store_true',
                        help='Use AFL-Opt')
    parser.add_argument('benchmark_dir',
                        help='Path to ALL_BENCHMARKS-* directory')

    return parser.parse_args()


def create_cmd(afl_fuzz_path, target, target_dir, engine, out_dir, fts_dir,
               afl_opt=False):
    """Create AFL command to run."""
    target_conf = TARGETS[target]

    cmd_args = [
        afl_fuzz_path,
        '-o', out_dir,
        '-m', 'none',
    ]

    if afl_opt:
        cmd_args.extend(['-L', '0'])
    if target_conf['seeds'] == SeedsLocation.EMPTY:
        cmd_args.extend(['-i', os.path.join(target_dir, 'empty-seed')])
    elif target_conf['seeds'] == SeedsLocation.RUNDIR:
        cmd_args.extend(['-i', os.path.join(target_dir, 'seeds')])
    elif target_conf['seeds'] == SeedsLocation.FUZZER_TEST_SUITE:
        cmd_args.extend(['-i', os.path.join(fts_dir, target, 'seeds')])

    if 'dict_path' in target_conf:
        cmd_args.extend(['-x', target_conf['dict_path']])
    if 'timeout' in target_conf:
        cmd_args.extend(['-t', target_conf['timeout']])
    cmd_args.extend(['--', os.path.join(target_dir,
                                        '%s-%s' % (target, engine))])

    return cmd_args


def run_cmd(cmd, timeout):
    """Run AFL command."""
    env = os.environ.copy()
    env['AFL_NO_UI'] = '1'
    env['AFL_NO_AFFINITY'] = '1'

    print('running `%s` (timeout %d secs)' % (' '.join(cmd), timeout))
    return subprocess.run(cmd, env=env, timeout=timeout, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def run_fuzzers(cmds, timeout, outlog):
    """Run the list of AFL commands."""
    num_processes = multiprocessing.cpu_count() // 3

    with multiprocessing.Pool(processes=num_processes) as pool, \
         open('%s.stdout' % outlog, 'wb') as stdout_log, \
         open('%s.stderr' % outlog, 'wb') as stderr_log:

        res = pool.starmap_async(run_cmd, product(cmds, (timeout,)))
        stdout = None
        stderr = None

        try:
            proc = res.get()
            stdout = proc.stdout
            stderr = proc.stderr
        except subprocess.CalledProcessError as proc:
            print('`%s` failed: %s' % (' '.join(proc.cmd), proc.stderr))
            raise
        except subprocess.TimeoutExpired as proc:
            stdout = proc.stdout
            stderr = proc.stderr

        if stdout:
            stdout_log.write(stdout)
        if stderr:
            stderr_log.write(stderr)

        pool.close()
        pool.join()


def main():
    """The main function."""
    args = parse_args()

    # Check paths
    afl_fuzz = os.path.join(args.afl_dir, 'afl-fuzz')
    if not os.path.isfile(afl_fuzz):
        raise Exception('AFL fuzzer not found in %s' % args.afl_dir)
    afl_fuzz = os.path.realpath(afl_fuzz)

    benchmark_dir = args.benchmark_dir
    if not os.path.isdir(benchmark_dir):
        raise Exception('ALL_BENCHMARKS-* directory does not exist at %s' %
                        benchmark_dir)
    benchmark_dir = os.path.realpath(benchmark_dir)

    fts_dir = args.fts
    if not os.path.isfile(os.path.join(fts_dir, 'common.sh')):
        raise Exception('Google FTS not found at %s' % fts_dir)
    fts_dir = os.path.realpath(fts_dir)

    engine = args.fuzzing_engine
    out_prefix = args.output_prefix

    # Create a list of AFL commands to run
    cmds = []
    for target in TARGETS:
        target_dir = os.path.join(benchmark_dir,
                                  'RUNDIR-%s-%s' % (engine, target))

        for i in range(1, args.num_trials + 1):
            out_dir = os.path.join(target_dir, '%s-%02d' % (out_prefix, i))
            cmd = create_cmd(afl_fuzz, target, target_dir, engine, out_dir,
                             fts_dir, args.afl_opt)
            cmds.append(cmd)

    # Run the fuzzers
    datetime_str = datetime.now().strftime('%Y-%m-%d-%H:%M:%S')
    outlog = os.path.join(benchmark_dir,
                          '%s-%s-%s' % (out_prefix, engine, datetime_str))
    run_fuzzers(cmds, args.timeout, outlog)


if __name__ == '__main__':
    main()
