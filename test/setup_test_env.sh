#!/bin/bash

# These directories have the Bazel meaning described at
# https://bazel.build/versions/master/docs/test-encyclopedia.html. In particular, TEST_SRCDIR is
# where we expect to find the generated outputs of various scripts preparing input data (these are
# not only the actual source files!).
# It is a precondition that both $TEST_TMPDIR and $TEST_SRCDIR are empty.
if [ -z "$TEST_TMPDIR" ] || [ -z "$TEST_SRCDIR" ]
then
  TEST_BASE=/tmp/envoy_test
  rm -rf $TEST_BASE
fi
: ${TEST_TMPDIR:=$TEST_BASE/tmp}
: ${TEST_SRCDIR:=$TEST_BASE/runfiles}
export TEST_TMPDIR TEST_SRCDIR
