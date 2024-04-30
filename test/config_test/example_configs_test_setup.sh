#!/bin/bash

set -e

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"

# Windows struggles with its own paths, so we have to help it out with `--force-local`
tar --force-local -xvf "$TEST_SRCDIR"/"$EXAMPLE_CONFIGS_TAR_PATH" -C "$DIR"

# find uses full path to prevent using Windows find on Windows.
# grep -v is used to exclude the secrets directory because secrets are yamls but are not configs to be tested.
/usr/bin/find "$DIR" -type f | grep -c .yaml | grep -v "config_test/secrets" >"$TEST_TMPDIR"/config-file-count.txt
