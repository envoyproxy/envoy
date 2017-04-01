#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

if [[ "$1" == "bazel.debug" ]]; then
  echo "debug bazel build with tests..."
  cd ci
  ln -sf /thirdparty prebuilt
  ln -sf /thirdparty_build prebuilt
  # Not sandboxing, since non-privileged Docker can't do nested namespaces.
  export CC=gcc-4.9
  export USER=bazel
  export TEST_TMPDIR=/source/build
  BAZEL_OPTIONS="--strategy=CppCompile=standalone --strategy=CppLink=standalone \
    --strategy=TestRunner=standalone --strategy=ProtoCompile=standalone \
    --strategy=Genrule=standalone --verbose_failures --package_path %workspace%:.."
  [[ "$BAZEL_INTERACTIVE" == "1" ]] && BAZEL_BATCH="" || BAZEL_BATCH="--batch"
  [[ "$BAZEL_EXPUNGE" == "1" ]] && bazel clean --expunge
  echo "Building..."
  bazel $BAZEL_BATCH build $BAZEL_OPTIONS //source/exe:envoy-static
  echo "Testing..."
  bazel $BAZEL_BATCH test $BAZEL_OPTIONS --define force_test_link_static=yes --test_output=all \
    //test/...
  exit 0
fi

. "$(dirname "$0")"/build_setup.sh

echo "building using $NUM_CPUS CPUs"

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
