#!/bin/bash

set -e

# Workaround for argument too long issue in protoc
ulimit -s 16384

# bazel uses jgit internally and the default circle-ci .gitconfig says to
# convert https://github.com to ssh://git@github.com, which jgit does not support.
if [[ -e "~/.gitconfig" ]]; then
  mv ~/.gitconfig ~/.gitconfig_save
fi

# Workaround for not using ci/run_envoy_docker.sh
# Create a fake home. Python site libs tries to do getpwuid(3) if we don't and the CI
# Docker image gets confused as it has no passwd entry when running non-root
# unless we do this.
FAKE_HOME=/tmp/fake_home
mkdir -p "${FAKE_HOME}"
export HOME="${FAKE_HOME}"
export PYTHONUSERBASE="${FAKE_HOME}"
export USER=bazel

export ENVOY_SRCDIR="$(pwd)"

# xlarge resource_class.
# See note: https://circleci.com/docs/2.0/configuration-reference/#resource_class for why we
# hard code this (basically due to how docker works).
export NUM_CPUS=6

# CircleCI doesn't support IPv6 by default, so we run all tests with IPv4 only.
# IPv6 tests are run with Azure Pipelines.
export BAZEL_BUILD_EXTRA_OPTIONS+="--test_env=ENVOY_IP_TEST_VERSIONS=v4only --local_cpu_resources=${NUM_CPUS} \
  --action_env=HOME --action_env=PYTHONUSERBASE --test_env=HOME --test_env=PYTHONUSERBASE"

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

ci/do_ci.sh $*
