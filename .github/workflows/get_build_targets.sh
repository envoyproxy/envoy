#!/bin/bash


# This limits the directory that bazel query is going to search under.
readonly SEARCH_FOLDER="//source/common/..."

set -e -o pipefail

# Fetching the upstream HEAD to compare with.
git fetch https://github.com/envoyproxy/envoy.git master 2>/dev/null

git diff --name-only HEAD FETCH_HEAD | while IFS= read -r line
do
  # Only targets under those folders.
  case "$line" in
    source/*|include/*)
      bazel query "rdeps($SEARCH_FOLDER, $line, 1)" 2>/dev/null
      ;;
  esac
  # Limit to the first 10 targets.
done | sort -u | head -n 10 | cat
