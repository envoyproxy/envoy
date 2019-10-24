#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

[[ "$1" == "check" || "$1" == "fix" ]] || (echo "Usage: $0 <check|fix>"; exit 1)

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

# Find all source protos.
declare -r PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, @envoy_api//docs:protos))")

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api//docs:protos target in a few places as a result. This is not
# guaranteed to be the precise set of protos we want to format, but as a
# starting place it seems reasonable. In the future, we should change the logic
# here.
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/type_whisperer/type_whisperer.bzl%type_whisperer_aspect --output_groups=types_pb_text \
  --host_force_python=PY3
declare -x -r TYPE_DB_PATH="${PWD}"/source/common/config/api_type_db.generated.pb_text
bazel run ${BAZEL_BUILD_OPTIONS} //tools/type_whisperer:typedb_gen --spawn_strategy=standalone -- \
  ${PWD} ${TYPE_DB_PATH} ${PROTO_TARGETS}
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protoxform/protoxform.bzl%protoxform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --action_env=TYPE_DB_PATH --host_force_python=PY3

./tools/proto_sync.py "$1" ${PROTO_TARGETS}
