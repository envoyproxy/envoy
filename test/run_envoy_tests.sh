#!/bin/bash

set -e

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
export TEST_UDSDIR="$TEST_TMPDIR"

echo "TEST_TMPDIR=$TEST_TMPDIR"
echo "TEST_SRCDIR=$TEST_SRCDIR"

mkdir -p $TEST_TMPDIR

# This places the unittest SSL certificates into $TEST_TMPDIR where the unit
# tests in test/common/ssl expect to consume them
$SOURCE_DIR/test/common/ssl/gen_unittest_certs.sh

# Some hacks for the config file template substitution. These go away in the Bazel build.
CONFIG_IN_DIR="$SOURCE_DIR"/test/config/integration
CONFIG_RUNFILES_DIR="$TEST_SRCDIR/$TEST_WORKSPACE"/test/config/integration
CONFIG_OUT_DIR="$TEST_TMPDIR"/test/config/integration
mkdir -p "$CONFIG_RUNFILES_DIR"
mkdir -p "$CONFIG_OUT_DIR"
cp "$CONFIG_IN_DIR"/*.json "$CONFIG_RUNFILES_DIR"
for f in $(cd "$SOURCE_DIR"; find test/config/integration -name "*.json")
do
  "$SOURCE_DIR"/test/test_common/environment_sub.sh "$f"
done

# Some hacks for the runtime test filesystem. These go away in the Bazel build.
TEST_RUNTIME_DIR="$TEST_SRCDIR/$TEST_WORKSPACE"/test/common/runtime/test_data
mkdir -p "$TEST_RUNTIME_DIR"
cp -r "$SOURCE_DIR"/test/common/runtime/test_data/* "$TEST_RUNTIME_DIR"
"$SOURCE_DIR"/test/common/runtime/filesystem_setup.sh

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
