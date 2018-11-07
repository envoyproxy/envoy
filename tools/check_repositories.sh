#!/bin/bash

set -eu

# Check whether any git repositories are defined.
# Git repository definition contains `commit` and `remote` fields.
if grep -nr "commit =\|remote =" --include=*.bzl .; then
  echo "Using git repositories is not allowed."
  echo "To ensure that all dependencies can be stored offline in distdir, only HTTP repositories are allowed."
  exit 1
fi

# Check whether number of defined `url =` or `urls =` and `sha256 =` kwargs in
# repository definitions is equal.
urls_count=$(git grep -E "url(s)? =" -- '*.bzl' | wc -l)
sha256sums_count=$(git grep -E "sha256 =" -- '*.bzl' | wc -l)

if [[ $urls_count != $sha256sums_count ]]; then
  echo "Found more defined repository URLs than SHA256 sums, which means that there are some repositories without sums."
  echo "Dependencies without SHA256 sums cannot be stored in distdir."
  echo "Please ensure that every repository has a SHA256 sum."
  echo "Repositories are defined in the following files:"
  echo ""
  echo "    bazel/repository_locations.bzl"
  echo "    api/bazel/repositories.bzl"
  exit 1
fi
