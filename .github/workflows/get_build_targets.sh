#!/bin/bash

# This limits the directory that bazel query is going to search under.
readonly SEARCH_FOLDER="//source/common/..."

set -e -o pipefail

function get_targets() {
  # Comparing the PR HEAD with the upstream main HEAD.
  git diff --name-only HEAD FETCH_HEAD | while IFS= read -r line
  do
    # Only targets under those folders.
    case "$line" in
      source/*|include/*)
        bazel query "rdeps($SEARCH_FOLDER, $line, 1)" 2>/dev/null
        ;;
    esac
    # This chain of commands from left to right are:
    # 1. Excluding the redundant .cc/.h targets that bazel query emits.
    # 2. Storing only the unique output.
    # 3. Limiting to the first 10 targets.
  done | grep -v '\.cc\|\.h' | sort -u | head -n 10
}

# Fetching the upstream HEAD to compare with and stored in FETCH_HEAD.
git fetch https://github.com/envoyproxy/envoy.git main 2>/dev/null

export BUILD_TARGETS_LOCAL=$(echo $(get_targets))
