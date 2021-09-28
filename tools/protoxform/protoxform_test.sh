#!/bin/bash

set -e

rm -rf bazel-bin/tools

read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=("--remote_download_outputs=all")
TOOLS="$(dirname "$(dirname "$(realpath "$0")")")"
# to satisfy dependency on run_command
export PYTHONPATH="$TOOLS"

# protoxform fix test cases
PROTO_TARGETS=()
protos=$(bazel query "labels(srcs, labels(deps, //tools/testdata/protoxform:fix_protos))")
while read -r line; do PROTO_TARGETS+=("$line"); done \
    <<< "$protos"
bazel build "${BAZEL_BUILD_OPTIONS[@]}" --//tools/api_proto_plugin:default_type_db_target=//tools/testdata/protoxform:fix_protos \
  //tools/testdata/protoxform:fix_protos --aspects //tools/protoxform:protoxform.bzl%protoxform_aspect --output_groups=proto
bazel build "${BAZEL_BUILD_OPTIONS[@]}" //tools/protoxform:protoprint
./tools/protoxform/protoxform_test_helper.py fix "${PROTO_TARGETS[@]}"
