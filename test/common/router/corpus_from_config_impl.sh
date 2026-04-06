#!/bin/bash

# Regenerates the fuzz corpus entries in route_corpus/generated/ from the
# config_impl_test unit tests.
#
# Usage:
#   bazel build -c fastbuild //test/common/router:config_impl_test
#   ./test/common/router/corpus_from_config_impl.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/route_corpus/generated"
TEST_BINARY="${SCRIPT_DIR}/../../../../bazel-bin/test/common/router/config_impl_test"

if [ ! -x "$TEST_BINARY" ]; then
  echo "Error: test binary not found at $TEST_BINARY"
  echo "Build it first: bazel build -c fastbuild //test/common/router:config_impl_test"
  exit 1
fi

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

if ! TEXT=$(NORUNFILES=1 GENRULE_OUTPUT_DIR="$OUTPUT_DIR" "$TEST_BINARY" 2>&1); then
  echo "$TEXT"
  echo "Router test failed to pass: debug logs above"
  exit 1
fi

COUNT=$(find "$OUTPUT_DIR" -maxdepth 1 -type f | wc -l)
echo "Generated $COUNT corpus entries in $OUTPUT_DIR"
