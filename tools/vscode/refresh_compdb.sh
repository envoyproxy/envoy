#!/usr/bin/env bash

[[ -z "${SKIP_PROTO_FORMAT}" ]] && tools/proto_format/proto_format.sh fix

# Setting TEST_TMPDIR here so the compdb headers won't be overwritten by another bazel run
TEST_TMPDIR=${BUILD_DIR:-/tmp}/envoy-compdb tools/gen_compilation_database.py --run_bazel_build -k

# Kill clangd to reload the compilation database
killall -v /opt/llvm/bin/clangd
