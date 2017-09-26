#!/bin/bash

set -e

# bazel uses jgit internally and the default circle-ci .gitconfig says to
# convert https://github.com to ssh://git@github.com, which jgit does not support.
mv ~/.gitconfig ~/.gitconfig_save

export ENVOY_SRCDIR="$(pwd)"

# xlarge resource_class.
# See note: https://circleci.com/docs/2.0/configuration-reference/#resource_class for why we
# hard code this (basically due to how docker works).
export NUM_CPUS=8

# Circle does not currently support IPv6 so we don't test it here. We will add and remove Travis
# when it is supported.
export BAZEL_EXTRA_TEST_OPTIONS="--test_env=ENVOY_IP_TEST_VERSIONS=v4only"

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

ci/do_ci.sh "$1"
