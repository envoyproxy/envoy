#!/usr/bin/bash.exe

set -e

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

. "$(dirname "$0")"/setup_cache.sh

# Set up TMPDIR so bash and non-bash can access
# e.g. TMPDIR=/d/tmp, make a link from /d/d to /d so both bash and Windows programs resolve the
# same path
# This is due to this issue: https://github.com/bazelbuild/rules_foreign_cc/issues/334
# rules_foreign_cc does not currently use bazel output/temp directories by default, it uses mktemp
# which respects the value of the TMPDIR environment variable
drive="$(readlink -f $TMPDIR | cut -d '/' -f2)"
if [ ! -e "/$drive/$drive" ]; then
  /c/windows/system32/cmd.exe /c "mklink /d $drive:\\$drive $drive:\\"
fi

BAZEL_STARTUP_OPTIONS="--output_base=c:/_eb"
BAZEL_BUILD_OPTIONS="-c opt --config=msvc-cl --show_task_finish --verbose_failures \
  --test_output=errors ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //source/exe:envoy-static --build_tag_filters=-skip_on_windows

# Test invocations of known-working tests on Windows
bazel ${BAZEL_STARTUP_OPTIONS} test ${BAZEL_BUILD_OPTIONS} //test/... --test_tag_filters=-skip_on_windows,-fails_on_windows --build_tests_only

# Build tests that are failing to ensure no regressions
bazel ${BAZEL_STARTUP_OPTIONS} build ${BAZEL_BUILD_OPTIONS} //test/... --test_tag_filters=-skip_on_windows,fails_on_windows --build_tests_only
