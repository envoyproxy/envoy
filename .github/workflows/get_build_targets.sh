#!/bin/bash

readonly TMP_OUTPUT_FILE="tmp.txt"

# This limits the directory that bazel query is going to search under.
readonly SEARCH_FOLDER="//source/common/..."

set -e -o pipefail

git fetch https://github.com/envoyproxy/envoy.git master 2>/dev/null

git diff --name-only $ORIGINAL_HEAD FETCH_HEAD | while IFS= read -r line
do
  # Only targets under those folders.
  case "$line" in
    source/*|include/*)
      bazel query "rdeps($SEARCH_FOLDER, $line, 1)" 2>/dev/null
      ;;
  esac
  # Limit to the first 10 targets.
done | sort -u | head -n 10 | tee $TMP_OUTPUT_FILE

export BUILD_TARGETS=$(cat $TMP_OUTPUT_FILE)
rm $TMP_OUTPUT_FILE
