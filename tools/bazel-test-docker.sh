#!/bin/bash

# Run a single Bazel test target under a privileged docker. Usage:
#
# tools/bazel-test-docker //test/foo:bar --some_other --bazel_args
# By default, this will run in a local docker container, mounting the local shared library paths
# into the counter. To run remotely, use RUN_REMOTE=yes. If the test was compiled with a different
# toolchain than the envoy-build container, passing in LOCAL_MOUNT=yes will force it to copy the
# local libraries into the container.

if [[ ! "$1" =~ (@[a-zA-Z0-9_-]+)?//.*:[a-zA-Z0-9_-]+ ]]
then
  echo "First argument to $0 must be a [@repo]//test/foo:bar label identifying a single test to run"
  echo "$1 does not match this pattern"
  exit 1
fi

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
[[ -z "${BAZEL}" ]] && BAZEL=bazel
[[ -z "${DOCKER}" ]] && DOCKER=docker

if [[ -z "$RUN_REMOTE" ]]
then
  LOCAL_MOUNT=${LOCAL_MOUNT:-"yes"}
  RUN_REMOTE=no
else
  LOCAL_MOUNT=${LOCAL_MOUNT:-"no"}
  RUN_REMOTE=yes
fi

. ./ci/envoy_build_sha.sh
IMAGE=envoyproxy/envoy-build:${ENVOY_BUILD_SHA}

# Note docker_wrapper.sh is tightly coupled to the order of arguments here due to where the test
# name is passed in.
"${BAZEL}" test "$@" --strategy=TestRunner=standalone --cache_test_results=no \
  --test_output=summary --run_under="${SCRIPT_DIR}/docker_wrapper.sh ${IMAGE} ${RUN_REMOTE} \
   ${LOCAL_MOUNT}"
