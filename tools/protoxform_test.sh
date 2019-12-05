#!/bin/bash

declare -x PROTO_TARGETS=//tools/testdata/protoxform:protos

BAZEL_BUILD_OPTIONS+=" --remote_download_outputs=all --strategy=protoxform=sandboxed,local"
bazel build ${BAZEL_BUILD_OPTIONS} ${PROTO_TARGETS} --aspects \
  tools/type_whisperer/type_whisperer.bzl%type_whisperer_aspect --output_groups=types_pb_text \
  --host_force_python=PY3
declare -r PROTO_FILES=$(bazel query "labels(srcs, //tools/testdata/protoxform:protos)")
declare -x -r TYPE_DB_PATH="${PWD}"/source/common/config/api_type_db.generated.pb_text
bazel run ${BAZEL_BUILD_OPTIONS} //tools/type_whisperer:typedb_gen -- \
  ${PWD} ${TYPE_DB_PATH} ${PROTO_FILES}
bazel build ${BAZEL_BUILD_OPTIONS} ${PROTO_TARGETS} --aspects \
  tools/protoxform/protoxform.bzl%protoxform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --action_env=TYPE_DB_PATH --host_force_python=PY3

./tools/protoxform_test_helper.py ${PROTO_FILES}