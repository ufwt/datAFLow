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

import yaml
try:
    from yaml import CLoader as YamlLoader
except ImportError:
    from yaml import YamlLoader


class FTSLocation(Enum):
    """Describes the location of inputs seeds or an AFL dictionary."""
    FUZZER_TEST_SUITE = 1
    RUNDIR = 2


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
    parser.add_argument('-m', '--afl-opt', required=False, action='store_true',
                        help='Use AFL-Opt')
    parser.add_argument('config', help='Path to YAML config file')

    return parser.parse_args()


def parse_location(location_dict, target, target_dir, fts_dir):
    """Parse a location into an actual filesystem path."""
    location = FTSLocation[location_dict['location']]
    name = location_dict['name']
    path = None

    if location == FTSLocation.RUNDIR:
        path = os.path.join(target_dir, name)
    elif location == FTSLocation.FUZZER_TEST_SUITE:
        path = os.path.join(fts_dir, target, name)

    return path


def create_cmd(afl_fuzz_path, target_conf, target_dir, engine, out_dir, fts_dir,
               timeout=86400, afl_opt=False):
    """Create AFL command to run."""
    target = target_conf['name']

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

    cmd_args.extend(['-i', parse_location(target_conf['seeds'], target,
                                          target_dir, fts_dir)])

    if 'dict' in target_conf:
        cmd_args.extend(['-x', parse_location(target_conf['dict'], target,
                                              target_dir, fts_dir)])

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

    afl_fuzz = os.path.join(args.afl_dir, 'afl-fuzz')
    if not os.path.isfile(afl_fuzz):
        raise Exception('AFL fuzzer not found in %s' % args.afl_dir)
    afl_fuzz = os.path.realpath(afl_fuzz)

    config_path = args.config
    if not os.path.isfile(config_path):
        raise Exception('YAML config %s does not exist' % config_path)
    config_path = os.path.realpath(config_path)

    config = None
    with open(config_path, 'r') as config_file:
        config = yaml.load(config_file, Loader=YamlLoader)
    if not config:
        raise Exception('YAML config %s is empty' % config_path)

    benchmark_dir = config['targets_dir']
    if not os.path.isdir(benchmark_dir):
        raise Exception('Fuzzer benchmark directory %s is invalid' %
                        benchmark_dir)
    benchmark_dir = os.path.realpath(benchmark_dir)

    fts_dir = config['fuzzer_test_suite_dir']
    if not os.path.isdir(fts_dir):
        raise Exception('Fuzzer Test Suite directory %s is invalid' % fts_dir)
    fts_dir = os.path.realpath(fts_dir)

    engine = args.fuzzing_engine
    out_prefix = args.output_prefix

    # Create a list of AFL commands to run
    cmds = []
    for target_conf in config['targets']:
        target = target_conf['name']
        target_dir = os.path.join(benchmark_dir,
                                  'RUNDIR-%s-%s' % (engine, target))

        for i in range(1, args.num_trials + 1):
            out_dir = os.path.join(target_dir, '%s-%02d' % (out_prefix, i))
            cmd = create_cmd(afl_fuzz, target_conf, target_dir, engine, out_dir,
                             fts_dir, args.timeout, args.afl_opt)
            cmds.append(cmd)

    # Run the fuzzers
    run_fuzzers(cmds, args.jobs)


if __name__ == '__main__':
    main()
