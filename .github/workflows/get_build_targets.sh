#!/bin/bash

# This limits the directory that bazel query is going to search under.
readonly SEARCH_FOLDER="//source/common/..."

set -e -o pipefail

# This is to filter out the redudant .cc or .h build targets that bazel
# query emits when rdeps = 0
function filter_line() {
  while read line
  do
    case "$line" in
        *.cc|*.h)
          ;;
        *)
          echo "$line"
          ;;
    esac
  done < "${1:-/dev/stdin}"
}

function get_targets() {
  # Comparing the PR HEAD with the upstream master HEAD.
  git diff --name-only HEAD FETCH_HEAD | while IFS= read -r line
  do
    # Only targets under those folders.
    case "$line" in
      source/*|include/*)
        bazel query "rdeps($SEARCH_FOLDER, $line, 1)" 2>/dev/null
        ;;
    esac
    # Limit to the first 10 targets.
  done | filter_line | sort -u | head -n 10
}

# Fetching the upstream HEAD to compare with and stored in FETCH_HEAD.
git fetch https://github.com/envoyproxy/envoy.git master 2>/dev/null

export BUILD_TARGETS_LOCAL=$(echo $(get_targets))
