#!/usr/bin/env bash

set -e


DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"

tar --force-local -xvf "$CONFIGS_TAR_PATH" -C "$DIR"

find "$DIR" -type f | grep -c .yaml > "$TEST_TMPDIR"/config-file-count.txt
