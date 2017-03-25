#!/bin/bash

# These directories have the Bazel meaning described at
# https://bazel.build/versions/master/docs/test-encyclopedia.html. In particular, TEST_SRCDIR is
# where we expect to find the generated outputs of various scripts preparing input data (these are
# not only the actual source files!).
: ${TEST_TMPDIR:=/tmp/envoy_test_tmp}
: ${TEST_SRCDIR:=/tmp/envoy_test_runfiles}
export TEST_TMPDIR TEST_SRCDIR
