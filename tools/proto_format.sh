#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api//docs:protos target in a few places as a result. This is not
# the precise set of protos we want to format, but as a starting place it seems
# reasonable. In the future, we should change the logic here.
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protoxform/protoxform.bzl%proto_xform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --spawn_strategy=standalone --host_force_python=PY3

declare -r DOCS_DEPS=$(bazel query "labels(deps, @envoy_api//docs:protos)")

# Copy protos from Bazel build-cache back into source tree.
for PROTO_TARGET in ${DOCS_DEPS}
do
  for p in $(bazel query "labels(srcs, ${PROTO_TARGET})" )
  do
    declare PROTO_TARGET_WITHOUT_PREFIX="${PROTO_TARGET#@envoy_api//}"
    declare PROTO_TARGET_CANONICAL="${PROTO_TARGET_WITHOUT_PREFIX/://}"
    declare PROTO_FILE_WITHOUT_PREFIX="${p#@envoy_api//}"
    declare PROTO_FILE_CANONICAL="${PROTO_FILE_WITHOUT_PREFIX/://}"
    declare DEST="api/${PROTO_FILE_CANONICAL}"

    if [[ "$1" == "fix" ]]
    then
      [[ -f "${DEST}" ]]
      cp bazel-bin/external/envoy_api/"${PROTO_TARGET_CANONICAL}/${PROTO_FILE_CANONICAL}.proto" "${DEST}"
    else
      diff bazel-bin/external/envoy_api/"${PROTO_TARGET_CANONICAL}/${PROTO_FILE_CANONICAL}.proto" "${DEST}" || \
        (echo "$0 mismatch, either run ./ci/do_ci.sh fix_format or $0 fix to reformat."; exit 1)
    fi
  done
done
