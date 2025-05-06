#!/bin/sh

# Helper shell script for :corpus_from_config_impl genrule in BUILD.

# Set NORUNFILES so test/main doesn't fail when runfiles manifest is not found.
SHARDS=5
for INDEX in $(seq 0 $((SHARDS-1))) ; do
  if ! TEXT=$(NORUNFILES=1 GTEST_TOTAL_SHARDS=$SHARDS GTEST_SHARD_INDEX=$INDEX "$@" 2>&1); then
    echo "$TEXT"
    echo "Router test failed to pass: debug logs above"
    exit 1
  fi
done

set -e

# Verify at least one entry is actually generated
[ -e "${GENRULE_OUTPUT_DIR}"/generated_corpus_0 ]
