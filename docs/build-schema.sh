#!/usr/bin/env bash

. tools/shell_utils.sh

set -e

source_venv "$BUILD_DIR"

[[ -z "${SCHEMA_OUTPUT_DIR}" ]] && SCHEMA_OUTPUT_DIR=generated/schema
[[ -z "${GENERATED_RST_DIR}" ]] && GENERATED_RST_DIR=generated/rst

rm -rf "${SCHEMA_OUTPUT_DIR}"
mkdir -p "${SCHEMA_OUTPUT_DIR}"

EXTENSION_DB_PATH="/build/extensions/extension_db.json"
export EXTENSION_DB_PATH

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=(
    "--remote_download_outputs=all"
    "--strategy=protoschema=sandboxed,local"
    "--action_env=PYTHONIOENCODING=UTF-8"
    "--action_env=ENVOY_BLOB_SHA"
    "--action_env=EXTENSION_DB_PATH")

bazel build "${BAZEL_BUILD_OPTIONS[@]}" //tools/protoschema:protoschema
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/protoschema:protoschema

# not sure how best to move files from bazel build, so /tmp workaround
cp -a /tmp/schema/envoy.schema.json "${SCHEMA_OUTPUT_DIR}"
