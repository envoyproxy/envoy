#!/usr/bin/env bash

[[ -z "${SKIP_PROTO_FORMAT}" ]] && tools/proto_format/proto_format.sh fix

bazel_or_isk=bazelisk
command -v bazelisk &> /dev/null || bazel_or_isk=bazel

opts=(--vscode --bazel="$bazel_or_isk")

[[ -z "${EXCLUDE_CONTRIB}" ]] || opts+=(--exclude_contrib)

# Setting TEST_TMPDIR here so the compdb headers won't be overwritten by another bazel run
TEST_TMPDIR=${BUILD_DIR:-/tmp}/envoy-compdb tools/gen_compilation_database.py \
    "${opts[@]}"

# Kill clangd to reload the compilation database
pkill clangd || :
