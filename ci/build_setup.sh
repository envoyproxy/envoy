#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

export PPROF_PATH=/thirdparty_build/bin/pprof

[ -z "${NUM_CPUS}" ] && NUM_CPUS=$(grep -c ^processor /proc/cpuinfo)
[ -z "${ENVOY_SRCDIR}" ] && export ENVOY_SRCDIR=/source
[ -z "${ENVOY_BUILD_TARGET}" ] && export ENVOY_BUILD_TARGET=//source/exe:envoy-static
[ -z "${ENVOY_BUILD_DEBUG_INFORMATION}" ] && export ENVOY_BUILD_DEBUG_INFORMATION=//source/exe:envoy-static.dwp
[ -z "${ENVOY_CONTRIB_BUILD_TARGET}" ] && export ENVOY_CONTRIB_BUILD_TARGET=//contrib/exe:envoy-static
[ -z "${ENVOY_CONTRIB_BUILD_DEBUG_INFORMATION}" ] && export ENVOY_CONTRIB_BUILD_DEBUG_INFORMATION=//contrib/exe:envoy-static.dwp
[ -z "${ENVOY_BUILD_ARCH}" ] && {
    ENVOY_BUILD_ARCH=$(uname -m)
    export ENVOY_BUILD_ARCH
}

read -ra BAZEL_BUILD_EXTRA_OPTIONS <<< "${BAZEL_BUILD_EXTRA_OPTIONS:-}"
read -ra BAZEL_EXTRA_TEST_OPTIONS <<< "${BAZEL_EXTRA_TEST_OPTIONS:-}"
read -ra BAZEL_OPTIONS <<< "${BAZEL_OPTIONS:-}"

echo "ENVOY_SRCDIR=${ENVOY_SRCDIR}"
echo "ENVOY_BUILD_TARGET=${ENVOY_BUILD_TARGET}"
echo "ENVOY_BUILD_ARCH=${ENVOY_BUILD_ARCH}"

function setup_gcc_toolchain() {
  if [[ -n "${ENVOY_STDLIB}" && "${ENVOY_STDLIB}" != "libstdc++" ]]; then
    echo "gcc toolchain doesn't support ${ENVOY_STDLIB}."
    exit 1
  fi

  BAZEL_BUILD_OPTIONS+=("--config=gcc")

  if [[ -z "${ENVOY_RBE}" ]]; then
    export CC=gcc
    export CXX=g++
    export BAZEL_COMPILER=gcc
    echo "$CC/$CXX toolchain configured"
  else
    BAZEL_BUILD_OPTIONS+=("--config=remote-gcc")
  fi
}

function setup_clang_toolchain() {
  ENVOY_STDLIB="${ENVOY_STDLIB:-libc++}"
  if [[ -z "${ENVOY_RBE}" ]]; then
    if [[ "${ENVOY_STDLIB}" == "libc++" ]]; then
      BAZEL_BUILD_OPTIONS+=("--config=libc++")
    else
      BAZEL_BUILD_OPTIONS+=("--config=clang")
    fi
  else
    if [[ "${ENVOY_STDLIB}" == "libc++" ]]; then
      BAZEL_BUILD_OPTIONS+=("--config=remote-clang-libc++")
    else
      BAZEL_BUILD_OPTIONS+=("--config=remote-clang")
    fi
  fi
  echo "clang toolchain with ${ENVOY_STDLIB} configured"
}

export BUILD_DIR=${BUILD_DIR:-/build}
if [[ ! -d "${BUILD_DIR}" ]]
then
  echo "${BUILD_DIR} mount missing - did you forget -v <something>:${BUILD_DIR}? Creating."
  mkdir -p "${BUILD_DIR}"
fi

# Environment setup.
export TEST_TMPDIR="${TEST_TMPDIR:-$BUILD_DIR/tmp}"
export LLVM_ROOT="${LLVM_ROOT:-/opt/llvm}"
export PATH=${LLVM_ROOT}/bin:${PATH}
export CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

if [[ -f "/etc/redhat-release" ]]; then
  BAZEL_BUILD_EXTRA_OPTIONS+=("--copt=-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1")
fi

function cleanup() {
  # Remove build artifacts. This doesn't mess with incremental builds as these
  # are just symlinks.
  rm -rf "${ENVOY_SRCDIR}"/bazel-* clang.bazelrc
}

cleanup
trap cleanup EXIT

"$(dirname "$0")"/../bazel/setup_clang.sh "${LLVM_ROOT}"

[[ "${BUILD_REASON}" != "PullRequest" ]] && BAZEL_EXTRA_TEST_OPTIONS+=("--nocache_test_results")

# Use https://docs.bazel.build/versions/master/command-line-reference.html#flag--experimental_repository_cache_hardlinks
# to save disk space.
BAZEL_BUILD_OPTIONS=(
  "${BAZEL_OPTIONS[@]}"
  "--verbose_failures"
  "--experimental_generate_json_trace_profile"
  "--test_output=errors"
  "--noshow_progress"
  "--noshow_loading_progress"
  "--repository_cache=${BUILD_DIR}/repository_cache"
  "--experimental_repository_cache_hardlinks"
  "--action_env=CLANG_FORMAT"
  "${BAZEL_BUILD_EXTRA_OPTIONS[@]}"
  "${BAZEL_EXTRA_TEST_OPTIONS[@]}")

[[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]] && BAZEL_BUILD_OPTIONS+=(
  "--flaky_test_attempts=2"
  "--test_env=HEAPCHECK=")

[[ "${BAZEL_EXPUNGE}" == "1" ]] && bazel clean --expunge

# Also setup some space for building Envoy standalone.
export ENVOY_BUILD_DIR="${BUILD_DIR}"/envoy
mkdir -p "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
export ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

# This is where we copy the coverage report to.
export ENVOY_COVERAGE_ARTIFACT="${ENVOY_BUILD_DIR}"/generated/coverage.tar.gz

# This is where we copy the fuzz coverage report to.
export ENVOY_FUZZ_COVERAGE_ARTIFACT="${ENVOY_BUILD_DIR}"/generated/fuzz_coverage.tar.gz

# This is where we dump failed test logs for CI collection.
export ENVOY_FAILED_TEST_LOGS="${ENVOY_BUILD_DIR}"/generated/failed-testlogs
mkdir -p "${ENVOY_FAILED_TEST_LOGS}"

# This is where we copy the build profile to.
export ENVOY_BUILD_PROFILE="${ENVOY_BUILD_DIR}"/generated/build-profile
mkdir -p "${ENVOY_BUILD_PROFILE}"

export BUILDIFIER_BIN="${BUILDIFIER_BIN:-/usr/local/bin/buildifier}"
export BUILDOZER_BIN="${BUILDOZER_BIN:-/usr/local/bin/buildozer}"

# We set up an Envoy consuming project for test builds only if '-nofetch'
# is not set AND this is an Envoy build. For derivative builds where Envoy
# source tree is different than the current workspace, the setup step is
# skipped.
if [[ "$1" != "-nofetch" && "${ENVOY_SRCDIR}" == "$(bazel info workspace)" ]]; then
  # shellcheck source=ci/filter_example_setup.sh
  . "$(dirname "$0")"/filter_example_setup.sh
else
  echo "Skip setting up Envoy Filter Example."
fi

export ENVOY_BUILD_FILTER_EXAMPLE="${FILTER_WORKSPACE_SET:-0}"
