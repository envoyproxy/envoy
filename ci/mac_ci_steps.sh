#!/bin/bash

set -e

BAZEL_BUILD_OPTIONS="--curses=no --show_task_finish --verbose_failures"
BAZEL_TEST_OPTIONS="${BAZEL_BUILD_OPTIONS} --test_output=all"

# Build envoy and run tests as separate steps so that failure output
# is somewhat more deterministic (rather than interleaving the build
# and test steps).

bazel build ${BAZEL_BUILD_OPTIONS} //source/...
bazel build ${BAZEL_BUILD_OPTIONS} //test/...
bazel test ${BAZEL_TEST_OPTIONS} //test/...
