#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

export CC=gcc-4.9
export CXX=g++-4.9
export HEAPCHECK=normal
export PPROF_PATH=/thirdparty_build/bin/pprof

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

export ENVOY_SRCDIR=/source

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
export ENVOY_CONSUMER_SRCDIR="${BUILD_DIR}/envoy-consumer"

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
if [[ ! -a "${ENVOY_CONSUMER_SRCDIR}" ]]
then
  # TODO(htuch): Update to non-htuch when https://github.com/lyft/envoy/issues/404 is sorted.
  git clone https://github.com/htuch/envoy-consumer.git "${ENVOY_CONSUMER_SRCDIR}"
fi
cp -f "${ENVOY_SRCDIR}"/ci/WORKSPACE.consumer "${ENVOY_CONSUMER_SRCDIR}"/WORKSPACE

# This is the hash on https://github.com/htuch/envoy-consumer.git we pin to.
(cd "${ENVOY_CONSUMER_SRCDIR}" && git checkout 94e11fa753a1e787c82cccaec642eda5e5b61ed8)

# Also setup some space for building Envoy standalone.
export ENVOY_BUILD_DIR="${BUILD_DIR}"/envoy
mkdir -p "${ENVOY_BUILD_DIR}"
cp -f "${ENVOY_SRCDIR}"/ci/WORKSPACE "${ENVOY_BUILD_DIR}"

# This is where we copy build deliverables to.
export ENVOY_DELIVERY_DIR="${ENVOY_BUILD_DIR}"/source/exe
mkdir -p "${ENVOY_DELIVERY_DIR}"

# This is where we build for bazel.release* and bazel.dev.
export ENVOY_CI_DIR="${ENVOY_SRCDIR}"/ci

# Hack due to https://github.com/lyft/envoy/issues/838 and the need to have
# tools and bazel.rc available for build linkstamping.
mkdir -p "${ENVOY_CONSUMER_SRCDIR}"/tools
mkdir -p "${ENVOY_CI_DIR}"/tools
ln -sf "${ENVOY_SRCDIR}"/tools/bazel.rc "${ENVOY_CONSUMER_SRCDIR}"/tools/
ln -sf "${ENVOY_SRCDIR}"/tools/bazel.rc "${ENVOY_CI_DIR}"/tools/
mkdir -p "${ENVOY_CONSUMER_SRCDIR}"/bazel
mkdir -p "${ENVOY_CI_DIR}"/bazel
ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status "${ENVOY_CONSUMER_SRCDIR}"/bazel/
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
