#!/bin/bash

set -e
set -x

SOURCE_DIR=$1
BINARY_DIR=$2
EXTRA_SETUP_SCRIPT=$3

# These directories have the Bazel meaning described at
# https://bazel.build/versions/master/docs/test-encyclopedia.html. In particular, TEST_SRCDIR is
# where we expect to find the generated outputs of various scripts preparing input data (these are
# not only the actual source files!).
# It is a precondition that both $TEST_TMPDIR and $TEST_SRCDIR are empty.
if [ -z "$TEST_TMPDIR" ] || [ -z "$TEST_SRCDIR" ]
then
  TEST_BASE=/tmp/envoy_test
  echo "Cleaning $TEST_BASE"
  rm -rf $TEST_BASE
fi
: ${TEST_TMPDIR:=$TEST_BASE/tmp}
: ${TEST_SRCDIR:=$TEST_BASE/runfiles}
export TEST_TMPDIR TEST_SRCDIR
export TEST_WORKSPACE=""
export TEST_RUNDIR="${TEST_SRCDIR}"
export TEST_UDSDIR="$TEST_TMPDIR"

echo "TEST_TMPDIR=$TEST_TMPDIR"
echo "TEST_SRCDIR=$TEST_SRCDIR"

mkdir -p $TEST_TMPDIR
mkdir -p $TEST_RUNDIR
ln -sf "${SOURCE_DIR}/test" "${TEST_RUNDIR}"

if [ -n "$EXTRA_SETUP_SCRIPT" ]; then
  $EXTRA_SETUP_SCRIPT
fi

# First run the normal unit test suite
cd $SOURCE_DIR
$RUN_TEST_UNDER $BINARY_DIR/test/envoy-test $EXTRA_TEST_ARGS

if [ "$UNIT_TEST_ONLY" = "1" ]
then
  exit 0
fi

export ENVOY_BIN="$BINARY_DIR"/source/exe/envoy
"$SOURCE_DIR"/test/integration/hotrestart_test.sh
