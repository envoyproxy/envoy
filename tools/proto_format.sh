#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

[[ "$1" == "check" || "$1" == "fix" ]] || (echo "Usage: $0 <check|fix>"; exit 1)

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
BAZEL_BUILD_OPTIONS+=" --remote_download_outputs=all --strategy=protoxform=sandboxed,local"

. $(dirname "$0")/gen_api_type_database.sh
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protoxform/protoxform.bzl%protoxform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --action_env=TYPE_DB_PATH --host_force_python=PY3

./tools/proto_sync.py "$1" ${PROTO_TARGETS}
