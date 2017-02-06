#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

if [[ "$1" == "docs" ]]; then
  echo "docs build..."
  make docs
  exit 0
fi

. "$(dirname "$0")"/build_setup.sh

if [[ "$1" == "fix_format" ]]; then
  echo "fix_format..."
  make fix_format
  exit 0
elif [[ "$1" == "coverage" ]]; then
  echo "coverage build with tests..."
  TEST_TARGET="envoy.check-coverage"
elif [[ "$1" == "asan" ]]; then
  echo "asan build with tests..."
  TEST_TARGET="envoy.check"
elif [[ "$1" == "debug" ]]; then
  echo "debug build with tests..."
  TEST_TARGET="envoy.check"
elif [[ "$1" == "server_only" ]]; then
  echo "normal build server only..."
  TEST_TARGET="envoy"
else
  echo "normal build with tests..."
  TEST_TARGET="envoy.check"
fi

shift
export EXTRA_TEST_ARGS="$@"

make check_format
make -j$NUM_CPUS $TEST_TARGET
