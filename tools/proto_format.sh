#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api//docs:protos target in a few places as a result. This is not
# the precise set of protos we want to format, but as a starting place it seems
# reasonable. In the future, we should change the logic here.
bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protoxform/protoxform.bzl%proto_xform_aspect --output_groups=proto --action_env=CPROFILE_ENABLED=1 \
  --spawn_strategy=standalone --host_force_python=PY3

# We do ** matching below to deal with Bazel cache blah (source proto artifacts
# are nested inside source package targets).
shopt -s globstar

# Find all source protos.
declare -r PROTO_TARGET=$(bazel query "labels(srcs, labels(deps, @envoy_api//docs:protos))")

# Copy protos from Bazel build-cache back into source tree.
for p in ${PROTO_TARGET}
do
  declare PROTO_FILE_WITHOUT_PREFIX="${p#@envoy_api//}"
  declare PROTO_FILE_CANONICAL="${PROTO_FILE_WITHOUT_PREFIX/://}"
  # We use ** glob matching here to deal with the fact that we have something
  # like
  # bazel-bin/external/envoy_api/envoy/admin/v2alpha/pkg/envoy/admin/v2alpha/certs.proto.proto
  # and we don't want to have to do a nested loop and slow bazel query to
  # recover the canonical package part of the path.
  declare SRCS=(bazel-bin/external/envoy_api/**/"${PROTO_FILE_CANONICAL}.proto")
  # While we may have reformatted the file multiple times due to the transitive
  # dependencies in the aspect above, they all look the same. So, just pick an
  # arbitrary match and we're done.
  declare SRC="${SRCS[0]}"
  declare DST="api/${PROTO_FILE_CANONICAL}"

  if [[ "$1" == "fix" ]]
  then
    [[ -f "${DST}" ]]
    cp -f "${SRC}" "${DST}"
  else
    diff ${SRC} "${DST}" || \
      (echo "$0 mismatch, either run ./ci/do_ci.sh fix_format or $0 fix to reformat."; exit 1)
  fi
done
