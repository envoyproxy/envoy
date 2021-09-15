#!/bin/bash

tools="$(dirname "$(dirname "$(realpath "$0")")")"
root=$(realpath "$tools/..")

cd "$root" || exit 1
# to satisfy dependency on run_command (as done in tools/code_format/check_format_test_helper.sh)
export PYTHONPATH="$root"
./tools/api_proto_breaking_change_detector/detector_ci.py "$@"
