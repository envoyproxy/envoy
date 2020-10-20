#!/bin/bash

set -e

function no_change {
  echo "No change to **/repository_locations.bzl"
  exit 0
}

(./tools/git/modified_since_last_github_commit.sh . bzl | grep repository_locations) || no_change

./tools/dependency/release_dates.sh ./bazel/repository_locations.bzl
./tools/dependency/release_dates.sh ./api/bazel/repository_locations.bzl
