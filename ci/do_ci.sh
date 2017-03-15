#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

echo "building using $NUM_CPUS CPUs"

if [[ "$1" == "bazel.debug" ]]; then
  echo "debug bazel build with tests..."
  cd ci
  ln -sf /thirdparty prebuilt
  ln -sf /thirdparty_build prebuilt
  # Not sandboxing, since non-privileged Docker can't do nested namespaces.
  echo "Building..."
  export CC=gcc-4.9
  export CXX=g++-4.9
  TEST_TMPDIR=/source/build USER=bazel bazel build --strategy=CppCompile=standalone \
    --strategy=CppLink=standalone --verbose_failures --package_path %workspace%:.. \
    //source/...
  echo "Testing..."
  TEST_TMPDIR=/source/build USER=bazel bazel test --strategy=CppCompile=standalone \
    --strategy=CppLink=standalone --strategy=TestRunner=standalone \
    --verbose_failures --test_output=all --package_path %workspace%:.. \
    //test/...
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

[[ "$SKIP_CHECK_FORMAT" == "1" ]] || make check_format
make -j$NUM_CPUS $TEST_TARGET
