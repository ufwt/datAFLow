#
# Common variables/functions across all scripts
#


import os
import re
import shlex


# Regex for pulling out memory access information from a fuzzalloc log file
FUZZALLOC_LOG_MEM_ACCESS_RE = re.compile(r'__mem_access: accessing def site (0x[0-9a-f]+) from (0x[0-9a-f]+)')


class AFLCommandLine(object):
    """Represents an AFL command-line invocation"""

    # Regex for pulling out the command-line used to run AFL from fuzzer_stats
    FUZZER_STATS_CMD_LINE_RE = re.compile(r'^command_line +: (.+) -- (.+)')

    def _find_afl_cmd_line_arg(self, arg):
        try:
            arg_idx = self._afl_cmd_line_tok.index(arg)
        except ValueError:
            return None

        return self._afl_cmd_line_tok[arg_idx + 1]

    def __init__(self, fuzzer_stats):
        """
        Create an `AFLCommandLine` object.

        Args:
            fuzzer_stats: File-like object containing an AFL fuzzer_stats file.
        """
        self._afl_cmd_line_tok = None
        self._target_cmd_line = None

        for line in fuzzer_stats:
            match = AFLCommandLine.FUZZER_STATS_CMD_LINE_RE.match(line)
            if not match:
                continue

            self._target_cmd_line = match.group(2)

            # The AFL path is split/tokenized
            self._afl_cmd_line_tok = shlex.split(match.group(1))

        if not self._afl_cmd_line_tok or not self._target_cmd_line:
            raise Exception('%s is not a valid fuzzer_stats file' %
                            fuzzer_stats.name)

        # Assume that afl-fuzz is the first argument
        self._afl_path = self._afl_cmd_line_tok[0]

        # Find the input/output directory (required arguments)
        self._input_dir = self._find_afl_cmd_line_arg('-i')
        self._output_dir = self._find_afl_cmd_line_arg('-o')

        # Optional arguments
        self._timeout = self._find_afl_cmd_line_arg('-t')
        self._memory_limit = self._find_afl_cmd_line_arg('-m')
        self._dict_path = self._find_afl_cmd_line_arg('-x')

    @property
    def afl_path(self):
        return self._afl_path

    @property
    def input_dir(self):
        return self._input_dir

    @property
    def output_dir(self):
        return self._output_dir

    @property
    def timeout(self):
        return self._timeout

    @property
    def memory_limit(self):
        return self._memory_limit

    @property
    def dict_path(self):
        return self._dict_path

    @property
    def target_cmd_line(self):
        return self._target_cmd_line


def get_afl_command_line(fuzzer_stats_path):
    """Grab the command-line used to run AFL from fuzzer_stats."""
    if not os.path.isfile(fuzzer_stats_path):
        raise Exception('%s is not a valid fuzzer_stats file' %
                        fuzzer_stats_path)

    with open(fuzzer_stats_path, 'r') as fuzzer_stats:
        return AFLCommandLine(fuzzer_stats)
