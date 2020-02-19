#!/bin/bash

rm -rf bazel-bin/tools

declare -r PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, //tools/testdata/protoxform:protos))")

BAZEL_BUILD_OPTIONS+=" --remote_download_outputs=all"

bazel build ${BAZEL_BUILD_OPTIONS} --//tools/api_proto_plugin:default_type_db_target=//tools/testdata/protoxform:protos \
  //tools/testdata/protoxform:protos --aspects //tools/protoxform:protoxform.bzl%protoxform_aspect --output_groups=proto \
  --action_env=CPROFILE_ENABLED=1 --host_force_python=PY3

TOOLS=$(dirname $(dirname $(realpath $0)))
# to satisfy dependency on run_command
export PYTHONPATH="$TOOLS"

./tools/protoxform/protoxform_test_helper.py ${PROTO_TARGETS}
