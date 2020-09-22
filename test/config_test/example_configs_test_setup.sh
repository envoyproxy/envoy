#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"
tar -xvf "$TEST_SRCDIR"/envoy/configs/example_configs.tar -C "$DIR"

CONFIG_FILE_COUNT=$(find "$DIR" -type f | grep .yaml | wc -l)
echo "$CONFIG_FILE_COUNT" > "$TEST_TMPDIR"/config-file-count.txt
