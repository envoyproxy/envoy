#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

export HEAPCHECK=normal
export PPROF_PATH=/thirdparty_build/bin/pprof

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

export ENVOY_SRCDIR=/source

function setup_gcc_toolchain() {
  export CC=gcc
  export CXX=g++
  echo "$CC/$CXX toolchain configured"
}

function setup_clang_toolchain() {
  export CC=clang-5.0
  export CXX=clang++-5.0
  echo "$CC/$CXX toolchain configured"
}

# Create a fake home. Python site libs tries to do getpwuid(3) if we don't and the CI
# Docker image gets confused as it has no passwd entry when running non-root
# unless we do this.
FAKE_HOME=/tmp/fake_home
mkdir -p "${FAKE_HOME}"
export HOME="${FAKE_HOME}"
export PYTHONUSERBASE="${FAKE_HOME}"

export BUILD_DIR=/build
if [[ ! -d "${BUILD_DIR}" ]]
then
  echo "${BUILD_DIR} mount missing - did you forget -v <something>:${BUILD_DIR}?"
  exit 1
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
export TEST_TMPDIR=/build/tmp
export BAZEL="bazel"
# Not sandboxing, since non-privileged Docker can't do nested namespaces.
BAZEL_OPTIONS="--package_path %workspace%:/source"
export BAZEL_QUERY_OPTIONS="${BAZEL_OPTIONS}"
export BAZEL_BUILD_OPTIONS="--strategy=Genrule=standalone --spawn_strategy=standalone \
  --verbose_failures ${BAZEL_OPTIONS} --action_env=HOME --action_env=PYTHONUSERBASE \
  --jobs=${NUM_CPUS}"
export BAZEL_TEST_OPTIONS="${BAZEL_BUILD_OPTIONS} --test_env=HOME --test_env=PYTHONUSERBASE \
  --cache_test_results=no --test_output=all"
[[ "${BAZEL_EXPUNGE}" == "1" ]] && "${BAZEL}" clean --expunge
ln -sf /thirdparty "${ENVOY_SRCDIR}"/ci/prebuilt
ln -sf /thirdparty_build "${ENVOY_SRCDIR}"/ci/prebuilt

# Setup Envoy consuming project.
if [[ ! -a "${ENVOY_FILTER_EXAMPLE_SRCDIR}" ]]
then
  git clone https://github.com/lyft/envoy-filter-example.git "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
fi
cp -f "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/WORKSPACE

# This is the hash on https://github.com/lyft/envoy-filter-example.git we pin to.
(cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}" && git fetch origin && git checkout e90e73c222fec574ab50b9e72880a9c8885f4f96)

# Also setup some space for building Envoy standalone.
export ENVOY_BUILD_DIR="${BUILD_DIR}"/envoy
mkdir -p "${ENVOY_BUILD_DIR}"
cp -f "${ENVOY_SRCDIR}"/ci/WORKSPACE "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
export ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

# This is where we copy the coverage report to.
export ENVOY_COVERAGE_DIR="${ENVOY_BUILD_DIR}"/generated/coverage
mkdir -p "${ENVOY_COVERAGE_DIR}"

# This is where we build for bazel.release* and bazel.dev.
export ENVOY_CI_DIR="${ENVOY_SRCDIR}"/ci

# Hack due to https://github.com/lyft/envoy/issues/838 and the need to have
# tools and bazel.rc available for build linkstamping.
mkdir -p "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/tools
mkdir -p "${ENVOY_CI_DIR}"/tools
ln -sf "${ENVOY_SRCDIR}"/tools/bazel.rc "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/tools/
ln -sf "${ENVOY_SRCDIR}"/tools/bazel.rc "${ENVOY_CI_DIR}"/tools/
mkdir -p "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel
mkdir -p "${ENVOY_CI_DIR}"/bazel
ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel/
ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status "${ENVOY_CI_DIR}"/bazel/

function cleanup() {
  # Remove build artifacts. This doesn't mess with incremental builds as these
  # are just symlinks.
  rm -f "${ENVOY_SRCDIR}"/bazel-*
  rm -f "${ENVOY_CI_DIR}"/bazel-*
  rm -rf "${ENVOY_CI_DIR}"/bazel
  rm -rf "${ENVOY_CI_DIR}"/tools
}
trap cleanup EXIT
