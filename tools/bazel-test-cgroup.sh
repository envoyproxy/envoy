#!/usr/bin/env bash

# Run cgroup CPU detection tests in a privileged Docker container with CPU limits.
# This wrapper adds --cpus=2 to the Docker container to enable cgroup CPU limit detection.
#
# Usage:
#   tools/bazel-test-cgroup.sh //test/server:cgroup_cpu_simple_integration_test

set -e

if [[ -z "$1" ]]; then
  echo "First argument to $0 must be a test target like //test/server:cgroup_cpu_simple_integration_test"
  exit 1
fi

# Set Docker extra args to add CPU limits
export DOCKER_EXTRA_ARGS="--cpus=2"

# Run the test using the standard Docker test infrastructure
# Add compiler flags to suppress C++20 concept warnings that are treated as errors with -Werror
# Set test environment variables that were previously hardcoded in docker_wrapper.sh
# Filter to run only containerized tests
# Note: DOCKER_EXTRA_ARGS is passed as argument through bazel-test-docker.sh
exec "$(dirname "${BASH_SOURCE[0]}")/bazel-test-docker.sh" "$@" \
  --cxxopt="-Wno-missing-requires" \
  --host_cxxopt="-Wno-missing-requires" \
  --test_env=NORUNFILES=1 \
  --test_env=ENVOY_IP_TEST_VERSIONS=v4only \
  --test_tag_filters=containerized \
  --cache_test_results=no \
  --test_output=all
