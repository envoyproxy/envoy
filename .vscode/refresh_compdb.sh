#!/usr/bin/env bash

TEST_TMPDIR=${TEST_TMPDIR:-/tmp}/compdb tools/gen_compilation_database.py --run_bazel_build

# Kill clangd to reload the compilation database
killall -v /opt/llvm/bin/clangd