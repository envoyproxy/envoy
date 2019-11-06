# No hashbang here because this script is intended for Windows

set -e

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

. "$(dirname "$0")"/setup_cache.sh

BAZEL_STARTUP_OPTIONS="--bazelrc=windows/.bazelrc"
BAZEL_BUILD_OPTIONS="--show_task_finish --verbose_failures \
  --test_output=all ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //bazel/foreign_cc:nghttp2 //bazel/foreign_cc:event //bazel/foreign_cc:yaml

bazel ${BAZEL_STARTUP_OPTIONS} test ${BAZEL_BUILD_OPTIONS} @envoy_api//test/build/...
