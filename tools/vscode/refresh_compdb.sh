#!/usr/bin/env bash

[[ -z "${SKIP_PROTO_FORMAT}" ]] && tools/proto_format/proto_format.sh fix

opts=(--vscode)

[[ -z "${EXCLUDE_CONTRIB}" ]] || opts+=(--exclude_contrib)

# Setting TEST_TMPDIR here so the compdb headers won't be overwritten by another bazel run
TEST_TMPDIR=${BUILD_DIR:-/tmp}/envoy-compdb tools/gen_compilation_database.py \
    "${opts[@]}"

# Kill clangd to reload the compilation database
pkill clangd || :
