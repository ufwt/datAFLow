#!/usr/bin/env python3

"""
Run all AFL fuzzing campaigns.

Author: Adrian Herrera
"""


from argparse import ArgumentParser
from enum import Enum
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
    parser.add_argument('-j', '--jobs', required=False, action='store',
                        type=int, default=multiprocessing.cpu_count() // 2,
                        help='Maximum number of processes to run concurrently')
    parser.add_argument('-f', '--fts', required=True, action='store',
                        help='Path to the Google FTS source directory')
    parser.add_argument('-m', '--afl-opt', required=False, action='store_true',
                        help='Use AFL-Opt')
    parser.add_argument('benchmark_dir',
                        help='Path to ALL_BENCHMARKS-* directory')

    return parser.parse_args()


def create_cmd(afl_fuzz_path, target, target_dir, engine, out_dir, fts_dir,
               timeout=86400, afl_opt=False):
    """Create AFL command to run."""
    target_conf = TARGETS[target]

    cmd_args = [
        'time',
        '--verbose',
        'timeout',
        '--preserve-status',
        '%ds' % timeout,
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

    return {
        'cmd': cmd_args,
        'target': target,
        'engine': engine,
        'out_dir': out_dir,
    }


def run_cmd(cmd_dict):
    """Run AFL command."""
    env = os.environ.copy()
    env['AFL_NO_UI'] = '1'

    print('running `%s`' % ' '.join(cmd_dict['cmd']))
    proc = subprocess.run(cmd_dict['cmd'], env=env, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    return cmd_dict, proc

def write_logs(proc, out_dir):
    """Write logs from the given process."""
    with open(os.path.join(out_dir, 'stdout.log'), 'wb') as stdout_log, \
         open(os.path.join(out_dir, 'stderr.log'), 'wb') as stderr_log:
        if proc.stdout:
            stdout_log.write(proc.stdout)
        if proc.stderr:
            stderr_log.write(proc.stderr)


def run_fuzzers(cmds, num_processes):
    """Run the list of AFL commands."""
    with multiprocessing.Pool(processes=num_processes) as pool:
        try:
            results = pool.map_async(run_cmd, cmds).get()
            for cmd, proc in results:
                write_logs(proc, cmd['out_dir'])
        except subprocess.CalledProcessError as err:
            print('`%s` failed: %s' % (' '.join(err.cmd), err.stderr))
            raise

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
                             fts_dir, args.timeout, args.afl_opt)
            cmds.append(cmd)

    # Run the fuzzers
    run_fuzzers(cmds, args.jobs)


if __name__ == '__main__':
    main()
