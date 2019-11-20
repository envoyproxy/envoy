#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"
tar -xvf "$TEST_SRCDIR"/envoy/configs/example_configs.tar -C "$DIR"
