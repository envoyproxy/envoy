#!/usr/bin/bash.exe

set -e

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

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

# Optional arguments include //source/exe:envoy-static to build,
# //test/... to test all with flake handling and test tag filters
# (these are the default), either one or the other, or a list of
# explicit tests or patterns which does not perform tag exclusions,
# unless given as additional argument. (If we explicitly ask, we
# are determined to fight a broken test, whether it is tagged
# skip/fail on windows or not.)

if [[ $1 == "//source/exe:envoy-static" ]]; then
  BUILD_ENVOY_STATIC=1
  shift
  TEST_TARGETS=("${@}")
elif [[ $# -gt 0 ]]; then
  BUILD_ENVOY_STATIC=0
  TEST_TARGETS=("$@")
else
  BUILD_ENVOY_STATIC=1
  TEST_TARGETS=('//test/...')
fi

# Complete envoy-static build
if [[ $BUILD_ENVOY_STATIC -eq 1 ]]; then
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //source/exe:envoy-static

  # Copy binary and pdb to delivery directory
  cp -f bazel-bin/source/exe/envoy-static.exe "${ENVOY_DELIVERY_DIR}/envoy.exe"
  cp -f bazel-bin/source/exe/envoy-static.pdb "${ENVOY_DELIVERY_DIR}/envoy.pdb"

  # Copy for azp, creating a tar archive
  tar czf "${ENVOY_BUILD_DIR}"/envoy_binary.tar.gz -C "${ENVOY_DELIVERY_DIR}" envoy.exe
  tar czf "${ENVOY_BUILD_DIR}"/envoy_binary_debug.tar.gz -C "${ENVOY_DELIVERY_DIR}" envoy.exe envoy.pdb
fi

# Test invocations of known-working tests on Windows
if [[ "${TEST_TARGETS[*]}" == "//test/..." ]]; then
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" test "${BAZEL_BUILD_OPTIONS[@]}" "${TEST_TARGETS[@]}" --test_tag_filters=-skip_on_windows,-fails_on_${FAIL_GROUP} --build_tests_only

  # Build tests that are known flaky or failing to ensure no compilation regressions
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //test/... --test_tag_filters=fails_on_${FAIL_GROUP} --build_tests_only

  if [[ $BUILD_ENVOY_STATIC -eq 1 ]]; then
    # Validate introduction or updates of any dependency libraries in bazel/foreign_cc and bazel/external
    # not triggered by envoy-static or //test/... targets and not deliberately tagged skip_on_windows
    bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //bazel/... --build_tag_filters=-skip_on_windows
  fi
elif [[ -n "${TEST_TARGETS[*]}" ]]; then
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" test "${BAZEL_BUILD_OPTIONS[@]}" "${TEST_TARGETS[@]}" --build_tests_only
fi

# Summarize known unbuildable or inapplicable tests (example)
# bazel "${BAZEL_STARTUP_OPTIONS[@]}" query 'kind(".*test rule", attr("tags", "skip_on_windows", //test/...))' 2>/dev/null | sort
