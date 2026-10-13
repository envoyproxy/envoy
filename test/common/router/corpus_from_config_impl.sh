#!/bin/sh

# Helper shell script for :corpus_from_config_impl in BUILD.

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

# Verify at least one entry is actually generated.
[ -e "${GENRULE_OUTPUT_DIR}"/generated_corpus_0 ]

# Normalize the output as the test build side effect is not deterministic.
sed -i 's/[[:space:]]*$//' "${GENRULE_OUTPUT_DIR}"/*
sed -i 's|^goo\.gle/debug[a-z]*|goo.gle/debugproto|' "${GENRULE_OUTPUT_DIR}"/*

# Dedupe corpus files in-place: keep only the first occurrence of each unique
# file content (by md5), removing subsequent duplicates.
cd "${GENRULE_OUTPUT_DIR}"
md5sum -- * | sort -k1,1 | awk '
  seen[$1] { print $2; next }
  { seen[$1] = 1 }
' | xargs -r rm --
