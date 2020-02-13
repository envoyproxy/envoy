#!/bin/bash

tools=$(dirname $(dirname $(realpath $0)))
root=$(realpath $tools/..)

cd $root
# to satisfy dependency on run_command
export PYTHONPATH="$tools"
./tools/code_format/check_format_test_helper.py "$@"
