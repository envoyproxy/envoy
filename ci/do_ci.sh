#!/bin/bash

# Run a CI build/test target, e.g. docs, asan.

set -e

. "$(dirname "$0")"/build_setup.sh
echo "building using ${NUM_CPUS} CPUs"

if [[ "$1" == "bazel.release" ]]; then
  echo "bazel release build with tests..."
  cd "${ENVOY_CONSUMER_SRCDIR}"
  echo "Building..."
  bazel --batch build ${BAZEL_BUILD_OPTIONS} -c opt @envoy//source/exe:envoy-static.stripped.stamped
  # Copy the envoy-static binary somewhere that we can access outside of the
  # container.
  DELIVER_DIR="${ENVOY_BUILD_DIR}"/source/exe
  mkdir -p "${DELIVER_DIR}"
  cp -f \
    "${ENVOY_CONSUMER_SRCDIR}"/bazel-genfiles/external/envoy/source/exe/envoy-static.stripped.stamped \
    "${DELIVER_DIR}"/envoy
  echo "Testing..."
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c opt --test_output=all \
    --cache_test_results=no @envoy//test/... //:echo2_integration_test
  exit 0
elif [[ "$1" == "bazel.asan" ]]; then
  echo "bazel ASAN debug build with tests..."
  cd "${ENVOY_CONSUMER_SRCDIR}"
  echo "Building and testing..."
  # TODO(htuch): This should switch to using clang when available.
  bazel --batch test ${BAZEL_TEST_OPTIONS} -c dbg --config=asan --test_output=all \
    --cache_test_results=no @envoy//test/... //:echo2_integration_test
  exit 0
elif [[ "$1" == "bazel.fastbuild" ]]; then
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel fastbuild of envoy-static..."
  cd "${ENVOY_BUILD_DIR}"
  echo "Building..."
  bazel build ${BAZEL_BUILD_OPTIONS} -c fastbuild //source/exe:envoy-static
  exit 0
elif [[ "$1" == "bazel.fastbuild.test" ]]; then
  # This doesn't go into CI but is available for developer convenience.
  echo "bazel test with fastbuild..."
  cd "${ENVOY_BUILD_DIR}"
  echo "Building and testing..."
  bazel test ${BAZEL_BUILD_OPTIONS} -c fastbuild //test/...
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
  export BAZEL_TEST_OPTIONS="${BAZEL_TEST_OPTIONS} -c dbg"
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
else
  echo "Invalid do_ci.sh target, see ci/README.md for valid targets."
  exit 1
fi
