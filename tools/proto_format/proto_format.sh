#!/usr/bin/env bash

# Reformat API protos to canonical proto style using protoxform.

set -e

read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTION_LIST:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"


# This ensures caches are busted for the local api fileystem when it changes
BAZEL_VOLATILE_DIRTY="${BAZEL_VOLATILE_DIRTY:-1}"
export BAZEL_VOLATILE_DIRTY


[[ "$1" == "check" || "$1" == "fix" ]] || {
    echo "Usage: $0 <check|fix>"
    exit 1
}

PROTO_SYNC_CMD="$1"

# Copy back the FileDescriptorProtos that protoxform emitted to the source tree. This involves
# pretty-printing to format with protoprint.
bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" \
    --remote_download_minimal \
    --//tools/api_proto_plugin:default_type_db_target=@envoy_api//:all_protos \
    //tools/proto_format:proto_sync \
    -- "--mode=${PROTO_SYNC_CMD}" \
       --ci

# Dont run this in git hooks by default
if [[ -n "$CI_BRANCH" ]] || [[ "${FORCE_PROTO_FORMAT}" == "yes" ]]; then
    echo "Run buf tests"
    # Run buf lint from root directory with api/ as the target path
    # This avoids changing directory and Bazel server restart issues
    bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" @rules_buf_toolchains//:buf -- lint api/
fi
