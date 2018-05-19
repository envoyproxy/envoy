#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"
tar -xvf "$TEST_RUNDIR"/configs/example_configs.tar -C "$DIR"

# Extract the content of tar file, find all config files and save their names.
tar -tf "$TEST_RUNDIR"/configs/example_configs.tar  | grep "yaml\|json" > "$DIR"/all_config_files.txt
