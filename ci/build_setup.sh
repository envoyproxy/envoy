#!/bin/bash

# Configure environment variables for Bazel build and test.

# Note order is important in this file, we dont want to use bazel until
# it has been properly configured, and we are in the correct env (eg filter example)

set -e

if [[ -n "$NO_BUILD_SETUP" ]]; then
    return
fi

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

export ENVOY_BUILD_FILTER_EXAMPLE="${ENVOY_BUILD_FILTER_EXAMPLE:-0}"

read -ra BAZEL_BUILD_EXTRA_OPTIONS <<< "${BAZEL_BUILD_EXTRA_OPTIONS:-}"
read -ra BAZEL_EXTRA_TEST_OPTIONS <<< "${BAZEL_EXTRA_TEST_OPTIONS:-}"
read -ra BAZEL_STARTUP_EXTRA_OPTIONS <<< "${BAZEL_STARTUP_EXTRA_OPTIONS:-}"
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
  BAZEL_BUILD_OPTION_LIST="${BAZEL_BUILD_OPTIONS[*]}"
  export BAZEL_BUILD_OPTION_LIST
}

function setup_clang_toolchain() {
  if [[ -n "$CLANG_TOOLCHAIN_SETUP" ]]; then
    return
  fi
  export CLANG_TOOLCHAIN_SETUP=1
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

  BAZEL_BUILD_OPTION_LIST="${BAZEL_BUILD_OPTIONS[*]}"
  export BAZEL_BUILD_OPTION_LIST
  echo "clang toolchain with ${ENVOY_STDLIB} configured"
}

if [[ -z "${BUILD_DIR}" ]]; then
    echo "BUILD_DIR not set - defaulting to ~/.cache/envoy-bazel" >&2
    BUILD_DIR="${HOME}/.cache/envoy-bazel"
fi
if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "${BUILD_DIR} missing - Creating." >&2
    mkdir -p "${BUILD_DIR}"
fi
export BUILD_DIR

# Environment setup.
export ENVOY_TEST_TMPDIR="${ENVOY_TEST_TMPDIR:-$BUILD_DIR/tmp}"
export LLVM_ROOT="${LLVM_ROOT:-/opt/llvm}"
export PATH=${LLVM_ROOT}/bin:${PATH}

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

# NB: do not use bazel before here to ensure correct directories.
_bazel="$(which bazel)"

BAZEL_STARTUP_OPTIONS=(
    "${BAZEL_STARTUP_EXTRA_OPTIONS[@]}"
    "--output_user_root=${BUILD_DIR}/bazel_root"
    "--output_base=${BUILD_DIR}/bazel_root/base")

bazel () {
    local startup_options
    read -ra startup_options <<< "${BAZEL_STARTUP_OPTION_LIST:-}"
    # echo "RUNNING BAZEL (${PWD}): ${startup_options[*]} <> ${*}" >&2
    "$_bazel" "${startup_options[@]}" "$@"
}

export _bazel
export -f bazel

# Use https://docs.bazel.build/versions/master/command-line-reference.html#flag--experimental_repository_cache_hardlinks
# to save disk space.
BAZEL_GLOBAL_OPTIONS=(
  "--repository_cache=${BUILD_DIR}/repository_cache"
  "--experimental_repository_cache_hardlinks")
BAZEL_BUILD_OPTIONS=(
  "${BAZEL_OPTIONS[@]}"
  "${BAZEL_GLOBAL_OPTIONS[@]}"
  "--verbose_failures"
  "--experimental_generate_json_trace_profile"
  "${BAZEL_BUILD_EXTRA_OPTIONS[@]}"
  "${BAZEL_EXTRA_TEST_OPTIONS[@]}")


[[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]] && BAZEL_BUILD_OPTIONS+=(
  "--test_env=HEAPCHECK=")

if [[ -z "${ENVOY_RBE}" ]]; then
    BAZEL_BUILD_OPTIONS+=("--test_tmpdir=${ENVOY_TEST_TMPDIR}")
    echo "Setting test_tmpdir to ${ENVOY_TEST_TMPDIR}."
fi

BAZEL_STARTUP_OPTION_LIST="${BAZEL_STARTUP_OPTIONS[*]}"
BAZEL_BUILD_OPTION_LIST="${BAZEL_BUILD_OPTIONS[*]}"
BAZEL_GLOBAL_OPTION_LIST="${BAZEL_GLOBAL_OPTIONS[*]}"
export BAZEL_STARTUP_OPTION_LIST
export BAZEL_BUILD_OPTION_LIST
export BAZEL_GLOBAL_OPTION_LIST

if [[ -e "${LLVM_ROOT}" ]]; then
    "$(dirname "$0")/../bazel/setup_clang.sh" "${LLVM_ROOT}"
else
    echo "LLVM_ROOT not found, not setting up llvm."
fi

[[ "${BAZEL_EXPUNGE}" == "1" ]] && bazel clean "${BAZEL_BUILD_OPTIONS[@]}" --expunge

if [[ "${ENVOY_BUILD_ARCH}" == "x86_64" ]]; then
    ENVOY_BUILD_DIR="${BUILD_DIR}/envoy/x64"
else
    ENVOY_BUILD_DIR="${BUILD_DIR}/envoy/arm64"
fi

# Also setup some space for building Envoy standalone.
export ENVOY_BUILD_DIR
mkdir -p "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
export ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

# This is where we copy the coverage report to.
export ENVOY_COVERAGE_ARTIFACT="${ENVOY_BUILD_DIR}/generated/coverage.tar.zst"

# This is where we copy the fuzz coverage report to.
export ENVOY_FUZZ_COVERAGE_ARTIFACT="${ENVOY_BUILD_DIR}/generated/fuzz_coverage.tar.zst"

# This is where we dump failed test logs for CI collection.
export ENVOY_FAILED_TEST_LOGS="${ENVOY_BUILD_DIR}"/generated/failed-testlogs
mkdir -p "${ENVOY_FAILED_TEST_LOGS}"

# This is where we copy the build profile to.
export ENVOY_BUILD_PROFILE="${ENVOY_BUILD_DIR}"/generated/build-profile
mkdir -p "${ENVOY_BUILD_PROFILE}"

if [[ "${ENVOY_BUILD_FILTER_EXAMPLE}" == "true" ]]; then
  # shellcheck source=ci/filter_example_setup.sh
  . "$(dirname "$0")"/filter_example_setup.sh
else
  echo "Skip setting up Envoy Filter Example."
fi

export NO_BUILD_SETUP=1
