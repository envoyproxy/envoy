#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

[[ "$1" == "check" || "$1" == "fix" ]] || (echo "Usage: $0 <check|fix>"; exit 1)

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api//docs:protos target in a few places as a result. This is not
# the precise set of protos we want to format, but as a starting place it seems
# reasonable. In the future, we should change the logic here.
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protoxform/protoxform.bzl%protoxform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --spawn_strategy=standalone --host_force_python=PY3

# Find all source protos.
declare -r PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, @envoy_api//docs:protos))")

./tools/proto_sync.py "$1" ${PROTO_TARGETS}
