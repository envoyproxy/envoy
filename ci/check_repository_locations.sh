#!/bin/bash

set -e

function no_change {
  echo "No change to **/repository_locations.bzl"
  exit 0
}

# (./tools/git/modified_since_last_github_commit.sh . bzl | grep repository_locations) || no_change

read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"

bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:release_dates
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/dependency:release_dates_api
