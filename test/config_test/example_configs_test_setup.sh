#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"
tar -xvf "$TEST_RUNDIR"/configs/example_configs.tar -C "$DIR"
