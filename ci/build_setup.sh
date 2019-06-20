#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

export PPROF_PATH=/thirdparty_build/bin/pprof

[ -z "${NUM_CPUS}" ] && NUM_CPUS=`grep -c ^processor /proc/cpuinfo`
[ -z "${ENVOY_SRCDIR}" ] && export ENVOY_SRCDIR=/source
echo "ENVOY_SRCDIR=${ENVOY_SRCDIR}"

function setup_gcc_toolchain() {
  export CC=gcc
  export CXX=g++
  echo "$CC/$CXX toolchain configured"
}

function setup_clang_toolchain() {
  export PATH=/usr/lib/llvm-8/bin:$PATH
  export CC=clang
  export CXX=clang++
  export ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-8/bin/llvm-symbolizer
  echo "$CC/$CXX toolchain configured"
}

# Create a fake home. Python site libs tries to do getpwuid(3) if we don't and the CI
# Docker image gets confused as it has no passwd entry when running non-root
# unless we do this.
FAKE_HOME=/tmp/fake_home
mkdir -p "${FAKE_HOME}"
export HOME="${FAKE_HOME}"
export PYTHONUSERBASE="${FAKE_HOME}"

export BUILD_DIR=${BUILD_DIR:-/build}
if [[ ! -d "${BUILD_DIR}" ]]
then
  echo "${BUILD_DIR} mount missing - did you forget -v <something>:${BUILD_DIR}? Creating."
  mkdir -p "${BUILD_DIR}"
fi
export ENVOY_FILTER_EXAMPLE_SRCDIR="${BUILD_DIR}/envoy-filter-example"

# Make sure that /source doesn't contain /build on the underlying host
# filesystem, including via hard links or symlinks. We can get into weird
# loops with Bazel symlinking and gcovr's path traversal if this is true, so
# best to keep /source and /build in distinct directories on the host
# filesystem.
SENTINEL="${BUILD_DIR}"/bazel.sentinel
touch "${SENTINEL}"
if [[ -n "$(find -L "${ENVOY_SRCDIR}" -name "$(basename "${SENTINEL}")")" ]]
then
  rm -f "${SENTINEL}"
  echo "/source mount must not contain /build mount"
  exit 1
fi
rm -f "${SENTINEL}"

# Environment setup.
export USER=bazel
export TEST_TMPDIR=${BUILD_DIR}/tmp
export BAZEL="bazel"

if [[ -f "/etc/redhat-release" ]]
then
  export BAZEL_BUILD_EXTRA_OPTIONS="--copt=-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1 --action_env=PATH ${BAZEL_BUILD_EXTRA_OPTIONS}"
else
  export BAZEL_BUILD_EXTRA_OPTIONS="--action_env=PATH=/bin:/usr/bin:/usr/lib/llvm-8/bin --linkopt=-fuse-ld=lld ${BAZEL_BUILD_EXTRA_OPTIONS}"
fi

# Not sandboxing, since non-privileged Docker can't do nested namespaces.
export BAZEL_QUERY_OPTIONS="${BAZEL_OPTIONS}"
export BAZEL_BUILD_OPTIONS="--strategy=Genrule=standalone --spawn_strategy=standalone \
  --verbose_failures ${BAZEL_OPTIONS} --action_env=HOME --action_env=PYTHONUSERBASE \
  --jobs=${NUM_CPUS} --show_task_finish --experimental_generate_json_trace_profile \
  --test_env=HOME --test_env=PYTHONUSERBASE --cache_test_results=no --test_output=all \
  ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

[[ "${BAZEL_EXPUNGE}" == "1" ]] && "${BAZEL}" clean --expunge

if [ "$1" != "-nofetch" ]; then
  # Setup Envoy consuming project.
  if [[ ! -a "${ENVOY_FILTER_EXAMPLE_SRCDIR}" ]]
  then
    git clone https://github.com/envoyproxy/envoy-filter-example.git "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  fi

  # This is the hash on https://github.com/envoyproxy/envoy-filter-example.git we pin to.
  (cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}" && git fetch origin && git checkout -f 6c0625cb4cc9a21df97cef2a1d065463f2ae81ae)
  cp -f "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/WORKSPACE
fi

# Also setup some space for building Envoy standalone.
export ENVOY_BUILD_DIR="${BUILD_DIR}"/envoy
mkdir -p "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
export ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

# This is where we copy the coverage report to.
export ENVOY_COVERAGE_DIR="${ENVOY_BUILD_DIR}"/generated/coverage
mkdir -p "${ENVOY_COVERAGE_DIR}"

# This is where we dump failed test logs for CI collection.
export ENVOY_FAILED_TEST_LOGS="${ENVOY_BUILD_DIR}"/generated/failed-testlogs
mkdir -p "${ENVOY_FAILED_TEST_LOGS}"

# This is where we copy the build profile to.
export ENVOY_BUILD_PROFILE="${ENVOY_BUILD_DIR}"/generated/build-profile
mkdir -p "${ENVOY_BUILD_PROFILE}"

function cleanup() {
  # Remove build artifacts. This doesn't mess with incremental builds as these
  # are just symlinks.
  rm -rf "${ENVOY_SRCDIR}"/bazel-*
}

cleanup
trap cleanup EXIT

mkdir -p "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel
ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel/
cp -f "${ENVOY_SRCDIR}"/.bazelrc "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/

export BUILDIFIER_BIN="/usr/local/bin/buildifier"
