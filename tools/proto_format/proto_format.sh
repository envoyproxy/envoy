#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e

[[ "$1" == "check" || "$1" == "fix" ]] || (echo "Usage: $0 <check|fix>"; exit 1)

# Developers working on protoxform and other proto format tooling changes will need to override the
# following check by setting FORCE_PROTO_FORMAT=yes in the environment.
./tools/git/modified_since_last_github_commit.sh ./api/envoy proto || \
  [[ "${FORCE_PROTO_FORMAT}" == "yes" ]] || \
  { echo "Skipping proto_format.sh due to no API change"; exit 0; }

if [[ "$2" == "--test" ]]
then
  ./tools/protoxform/protoxform_test.sh
fi

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
# rm -rf bazel-bin/external/envoy_api

# Find all source protos.
declare -r PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, @envoy_api_canonical//docs:protos))")

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
BAZEL_BUILD_OPTIONS+=" --remote_download_outputs=all"

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api_canonical//docs:protos target in a few places as a result. This is not
# guaranteed to be the precise set of protos we want to format, but as a
# starting place it seems reasonable. In the future, we should change the logic
# here.
bazel build ${BAZEL_BUILD_OPTIONS} --//tools/api_proto_plugin:default_type_db_target=@envoy_api_canonical//docs:protos \
  @envoy_api_canonical//docs:protos --aspects //tools/protoxform:protoxform.bzl%protoxform_aspect --output_groups=proto \
  --action_env=CPROFILE_ENABLED=1 --host_force_python=PY3

TOOLS=$(dirname $(dirname $(realpath $0)))
# to satisfy dependency on api_proto_plugin
export PYTHONPATH="$TOOLS"
./tools/proto_format/proto_sync.py "--mode=$1" ${PROTO_TARGETS}

bazel build ${BAZEL_BUILD_OPTIONS} //tools/type_whisperer:api_build_file
cp -f bazel-bin/tools/type_whisperer/BUILD.api_build_file api/BUILD

cp -f  ./api/bazel/*.bzl ./api/bazel/BUILD ./generated_api_shadow/bazel
