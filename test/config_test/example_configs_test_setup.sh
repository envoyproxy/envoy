#!/usr/bin/env bash

set -e

# TODO(phlax): Cleanup once bzlmod migration is complete
# Detect workspace name - in WORKSPACE it's 'envoy', in bzlmod it could be '_main' or 'envoy~'
for workspace_name in envoy envoy~ _main; do
    if [[ -d "${TEST_SRCDIR}/${workspace_name}" ]]; then
        ENVOY_WORKSPACE="${workspace_name}"
        break
    fi
done
# Fallback to 'envoy' if detection fails
ENVOY_WORKSPACE="${ENVOY_WORKSPACE:-envoy}"

# Replace 'envoy/' prefix with detected workspace
EXAMPLE_CONFIGS_TAR_PATH="${EXAMPLE_CONFIGS_TAR_PATH/envoy\//${ENVOY_WORKSPACE}/}"

DIR="$TEST_TMPDIR"/test/config_test
mkdir -p "$DIR"

# Windows struggles with its own paths, so we have to help it out with `--force-local`
tar --force-local -xvf "$TEST_SRCDIR"/"$EXAMPLE_CONFIGS_TAR_PATH" -C "$DIR"

# find uses full path to prevent using Windows find on Windows.
/usr/bin/find "$DIR" -type f | grep -c .yaml > "$TEST_TMPDIR"/config-file-count.txt
