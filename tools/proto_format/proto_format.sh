#!/bin/bash

# Reformat API protos to canonical proto style using protoxform.

set -e
set -x

[[ "$1" == "check" || "$1" == "fix" || "$1" == "freeze" ]] || \
  (echo "Usage: $0 <check|fix|freeze>"; exit 1)

# Developers working on protoxform and other proto format tooling changes will need to override the
# following check by setting FORCE_PROTO_FORMAT=yes in the environment.
./tools/git/modified_since_last_github_commit.sh ./api/envoy proto || \
  [[ "${FORCE_PROTO_FORMAT}" == "yes" ]] || \
  { echo "Skipping proto_format.sh due to no API change"; exit 0; }

if [[ "$2" == "--test" ]]
then
  echo "protoxform_test..."
  ./tools/protoxform/protoxform_test.sh
  bazel test ${BAZEL_BUILD_OPTIONS} //tools/protoxform:merge_active_shadow_test
fi

# Generate //versioning:active_protos.
./tools/proto_format/active_protos_gen.py ./api > ./api/versioning/BUILD

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
BAZEL_BUILD_OPTIONS+=" --remote_download_outputs=all"

# If the specified command is 'freeze', we tell protoxform to adjust package version status to
# reflect a major version freeze and then do a regular 'fix'.
PROTO_SYNC_CMD="$1"
if [[ "$1" == "freeze" ]]
then
  declare -r FREEZE_ARG="--//tools/api_proto_plugin:extra_args=freeze"
  PROTO_SYNC_CMD="fix"
fi

# Invoke protoxform aspect.
bazel build ${BAZEL_BUILD_OPTIONS} --//tools/api_proto_plugin:default_type_db_target=@envoy_api_canonical//versioning:active_protos ${FREEZE_ARG} \
  @envoy_api_canonical//versioning:active_protos --aspects //tools/protoxform:protoxform.bzl%protoxform_aspect --output_groups=proto

# Find all source protos.
declare -r ACTIVE_PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, @envoy_api_canonical//versioning:active_protos))")
declare -r FROZEN_PROTO_TARGETS=$(bazel query "labels(srcs, labels(deps, @envoy_api_canonical//versioning:frozen_protos))")

# Setup for proto_sync.py.
TOOLS=$(dirname $(dirname $(realpath $0)))
# To satisfy dependency on api_proto_plugin.
export PYTHONPATH="$TOOLS"
# Build protoprint and merge_active_shadow_tools for use in proto_sync.py.
bazel build ${BAZEL_BUILD_OPTIONS} //tools/protoxform:protoprint //tools/protoxform:merge_active_shadow

# Copy back the FileDescriptorProtos that protoxform emittted to the source tree. This involves
# pretty-printing to format with protoprint and potentially merging active/shadow versions of protos
# with merge_active_shadow.
./tools/proto_format/proto_sync.py "--mode=${PROTO_SYNC_CMD}" ${ACTIVE_PROTO_TARGETS} ${FROZEN_PROTO_TARGETS}

# Need to regenerate //versioning:active_protos before building type DB below if freezing.
if [[ "$1" == "freeze" ]]
then
 ./tools/proto_format/active_protos_gen.py ./api > ./api/versioning/BUILD
fi

# Generate api/BUILD file based on updated type database.
bazel build ${BAZEL_BUILD_OPTIONS} //tools/type_whisperer:api_build_file
cp -f bazel-bin/tools/type_whisperer/BUILD.api_build_file api/BUILD

# Misc. manual copies to keep generated_api_shadow/ in sync with api/.
cp -f ./api/bazel/*.bzl ./api/bazel/BUILD ./generated_api_shadow/bazel
