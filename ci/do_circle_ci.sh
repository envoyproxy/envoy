#!/bin/bash

set -e

# Workaround for argument too long issue in protoc
ulimit -s 16384
ulimit -c unlimited

# bazel uses jgit internally and the default circle-ci .gitconfig says to
# convert https://github.com to ssh://git@github.com, which jgit does not support.
if [[ -e "~/.gitconfig" ]]; then
  mv ~/.gitconfig ~/.gitconfig_save
fi

export ENVOY_SRCDIR="$(pwd)"

# xlarge resource_class.
# See note: https://circleci.com/docs/2.0/configuration-reference/#resource_class for why we
# hard code this (basically due to how docker works).
export NUM_CPUS=8

# CircleCI doesn't support IPv6 by default, so we run all tests with IPv4 only.
# IPv6 tests are run with Azure Pipelines.
export BAZEL_EXTRA_TEST_OPTIONS="--test_env=ENVOY_IP_TEST_VERSIONS=v4only"

mkdir -p /tmp/debug
vmstat -Sm -t 5 >/tmp/debug/vmstats.out &
VMSTAT_PID=$!

echo "/tmp/debug/%h-%e-%p.core" >/proc/sys/kernel/core_pattern

function finish {
  echo "disk space at end of build:"
  df -h

  kill $VMSTAT_PID || true
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

ci/do_ci.sh $*
