#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

. "$(dirname "$0")"/build_setup.sh
echo "building using ${NUM_CPUS} CPUs"

if [[ "$1" == "bazel.debug" ]]; then
  echo "bazel debug build with tests..."
  cd "${ENVOY_CONSUMER_SRCDIR}"
  echo "Building..."
  mkdir -p tools
  ln -sf "${ENVOY_SRCDIR}"/tools/bazel.rc tools/
  mkdir -p bazel
  ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status bazel/
  bazel build ${BAZEL_BUILD_OPTIONS} @envoy//source/exe:envoy-static.stamped
  echo "Testing..."
  bazel test ${BAZEL_TEST_OPTIONS} --test_output=all \
    --cache_test_results=no @envoy//test/... //:echo2_integration_test
  exit 0
elif [[ "$1" == "bazel.coverage" ]]; then
  echo "bazel coverage build with tests..."
  export GCOVR="/thirdparty/gcovr-3.3/scripts/gcovr"
  export GCOVR_DIR="${ENVOY_BUILD_DIR}/bazel-envoy"
  export TESTLOGS_DIR="${ENVOY_BUILD_DIR}/bazel-testlogs"
  export BUILDIFIER_BIN="/usr/lib/go/bin/buildifier"
  # There is a bug in gcovr 3.3, where it takes the -r path,
  # in our case /source, and does a regex replacement of various
  # source file paths during HTML generation. It attempts to strip
  # out the prefix (e.g. /source), but because it doesn't do a match
  # and only strip at the start of the string, it removes /source from
  # the middle of the string, corrupting the path. The workaround is
  # to point -r in the gcovr invocation in run_envoy_bazel_coverage.sh at
  # some Bazel created symlinks to the source directory in its output
  # directory. Wow.
  cd "${ENVOY_BUILD_DIR}"
  SRCDIR="${GCOVR_DIR}" "${ENVOY_SRCDIR}"/test/run_envoy_bazel_coverage.sh
  exit 0
elif [[ "$1" == "fix_format" ]]; then
  echo "fix_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py fix
  exit 0
elif [[ "$1" == "check_format" ]]; then
  echo "check_format..."
  cd "${ENVOY_SRCDIR}"
  ./tools/check_format.py check
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

[[ "${SKIP_CHECK_FORMAT}" == "1" ]] || make check_format
make -j${NUM_CPUS} ${TEST_TARGET}
