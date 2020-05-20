#!/usr/bin/env bash

tools/proto_format/proto_format.sh fix

# Setting platform suffix here so the compdb headers won't be overwritten by another bazel run
BAZEL_BUILD_OPTIONS=--platform_suffix=-compdb tools/gen_compilation_database.py --run_bazel_build -k

# Kill clangd to reload the compilation database
killall -v /opt/llvm/bin/clangd
