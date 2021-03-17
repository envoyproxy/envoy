#!/usr/bin/bash.exe

set -e

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

# shellcheck source=ci/setup_cache.sh
. "$(dirname "$0")"/setup_cache.sh

[ -z "${ENVOY_SRCDIR}" ] && export ENVOY_SRCDIR=/c/source

read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTIONS:-}"
# Default to msvc-cl if not overridden
read -ra BAZEL_BUILD_EXTRA_OPTIONS <<< "${BAZEL_BUILD_EXTRA_OPTIONS:---config=msvc-cl}"
read -ra BAZEL_EXTRA_TEST_OPTIONS <<< "${BAZEL_EXTRA_TEST_OPTIONS:-}"

# Set up TMPDIR so bash and non-bash can access
# e.g. TMPDIR=/d/tmp, make a link from /d/d to /d so both bash and Windows programs resolve the
# same path
# This is due to this issue: https://github.com/bazelbuild/rules_foreign_cc/issues/334
# rules_foreign_cc does not currently use bazel output/temp directories by default, it uses mktemp
# which respects the value of the TMPDIR environment variable
drive="$(readlink -f "$TMPDIR" | cut -d '/' -f2)"
if [ ! -e "/$drive/$drive" ]; then
  /c/windows/system32/cmd.exe /c "mklink /d $drive:\\$drive $drive:\\"
fi

BUILD_DIR=${BUILD_DIR:-/c/build}
if [[ ! -d "${BUILD_DIR}" ]]
then
  echo "${BUILD_DIR} mount missing - did you forget -v <something>:${BUILD_DIR}? Creating."
  mkdir -p "${BUILD_DIR}"
fi

# Environment setup.
export TEST_TMPDIR=${BUILD_DIR}/tmp

[[ "${BUILD_REASON}" != "PullRequest" ]] && BAZEL_EXTRA_TEST_OPTIONS+=(--nocache_test_results)

BAZEL_STARTUP_OPTIONS+=("--output_base=${TEST_TMPDIR/\/c/c:}")
BAZEL_BUILD_OPTIONS=(
    -c opt
    --show_task_finish
    --verbose_failures
    "--test_output=errors"
    "--repository_cache=${BUILD_DIR/\/c/c:}/repository_cache"
    "${BAZEL_BUILD_EXTRA_OPTIONS[@]}"
    "${BAZEL_EXTRA_TEST_OPTIONS[@]}")

# Also setup some space for building Envoy standalone.
ENVOY_BUILD_DIR="${BUILD_DIR}"/envoy
mkdir -p "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

FAIL_GROUP=windows
if [[ "${BAZEL_BUILD_EXTRA_OPTIONS[*]}" =~ "clang-cl" ]]; then
  FAIL_GROUP=clang_cl
fi

# Test to validate updates of all dependency libraries in bazel/external and bazel/foreign_cc
# bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //bazel/... --build_tag_filters=-skip_on_windows

# Complete envoy-static build (nothing needs to be skipped, build failure indicates broken dependencies)
bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //source/exe:envoy-static

# Copy binary to delivery directory
cp -f bazel-bin/source/exe/envoy-static.exe "${ENVOY_DELIVERY_DIR}/envoy.exe"

# Copy for azp, creating a tar archive
tar czf "${ENVOY_BUILD_DIR}"/envoy_binary.tar.gz -C "${ENVOY_DELIVERY_DIR}" envoy.exe

# Test invocations of known-working tests on Windows
bazel "${BAZEL_STARTUP_OPTIONS[@]}" test "${BAZEL_BUILD_OPTIONS[@]}" //test/... --test_tag_filters=-skip_on_windows,-fails_on_${FAIL_GROUP} --build_tests_only

echo "running flaky test reporting script"
"${ENVOY_SRCDIR}"/ci/flaky_test/run_process_xml.sh "$CI_TARGET"

# Build tests that are known flaky or failing to ensure no compilation regressions
bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //test/... --test_tag_filters=-skip_on_windows,fails_on_${FAIL_GROUP} --build_tests_only

# Summarize known unbuildable or inapplicable tests (example)
# bazel "${BAZEL_STARTUP_OPTIONS[@]}" query 'kind(".*test rule", attr("tags", "skip_on_windows", //test/...))' 2>/dev/null | sort
