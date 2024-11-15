#!/usr/bin/env python

# This tool take the foo.dep.log output from a build recipe run under recipe_wrapper.sh on stdin and
# produces a profile of command execution time on the stdout.

from __future__ import print_function

import re
import sys


def print_profile(f):
    prev_cmd = None
    prev_timestamp = None
    for line in f:
        sr = re.match(r'\++ (\d+\.\d+) (.*)', line)
        if sr:
            timestamp, cmd = sr.groups()
            if prev_cmd:
                print('%.2f %s' % (float(timestamp) - float(prev_timestamp), prev_cmd))
            prev_timestamp, prev_cmd = timestamp, cmd


if __name__ == '__main__':
    print_profile(sys.stdin)
