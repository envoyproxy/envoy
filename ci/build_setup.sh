#!/bin/bash

# Configure environment variables for Bazel build and test.

set -e

export PPROF_PATH=/thirdparty_build/bin/pprof

[ -z "${NUM_CPUS}" ] && NUM_CPUS=`grep -c ^processor /proc/cpuinfo`
[ -z "${ENVOY_SRCDIR}" ] && export ENVOY_SRCDIR=/source
echo "ENVOY_SRCDIR=${ENVOY_SRCDIR}"

function setup_gcc_toolchain() {
  if [[ -z "${ENVOY_RBE}" ]]; then
    export CC=gcc
    export CXX=g++
    export BAZEL_COMPILER=gcc
    echo "$CC/$CXX toolchain configured"
  else
    export BAZEL_BUILD_OPTIONS="--config=rbe-toolchain-gcc ${BAZEL_BUILD_OPTIONS}"
  fi
}

function setup_clang_toolchain() {
  if [[ -z "${ENVOY_RBE}" ]]; then
    export BAZEL_BUILD_OPTIONS="--config=clang ${BAZEL_BUILD_OPTIONS}"
  else
    export BAZEL_BUILD_OPTIONS="--config=rbe-toolchain-clang ${BAZEL_BUILD_OPTIONS}"
  fi
  echo "clang toolchain configured"
}

function setup_clang_libcxx_toolchain() {
  if [[ -z "${ENVOY_RBE}" ]]; then
    export BAZEL_BUILD_OPTIONS="--config=libc++ ${BAZEL_BUILD_OPTIONS}"
  else
    export BAZEL_BUILD_OPTIONS="--config=rbe-toolchain-clang-libc++ ${BAZEL_BUILD_OPTIONS}"
  fi
  echo "clang toolchain with libc++ configured"
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

# Environment setup.
export USER=bazel
export TEST_TMPDIR=${BUILD_DIR}/tmp
export BAZEL="bazel"
export PATH=/opt/llvm/bin:$PATH
export CLANG_FORMAT=clang-format

if [[ -f "/etc/redhat-release" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS+="--copt=-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1"
fi

bazel/setup_clang.sh /opt/llvm

# Not sandboxing, since non-privileged Docker can't do nested namespaces.
export BAZEL_QUERY_OPTIONS="${BAZEL_OPTIONS}"
export BAZEL_BUILD_OPTIONS="--verbose_failures ${BAZEL_OPTIONS} --action_env=HOME --action_env=PYTHONUSERBASE \
  --local_cpu_resources=${NUM_CPUS} --show_task_finish --experimental_generate_json_trace_profile \
  --test_env=HOME --test_env=PYTHONUSERBASE --cache_test_results=no --test_output=all \
  --repository_cache=${BUILD_DIR}/repository_cache --experimental_repository_cache_hardlinks \
  ${BAZEL_BUILD_EXTRA_OPTIONS} ${BAZEL_EXTRA_TEST_OPTIONS}"

[[ "${BAZEL_EXPUNGE}" == "1" ]] && "${BAZEL}" clean --expunge

if [ "$1" != "-nofetch" ]; then
  # Setup Envoy consuming project.
  if [[ ! -a "${ENVOY_FILTER_EXAMPLE_SRCDIR}" ]]
  then
    git clone https://github.com/envoyproxy/envoy-filter-example.git "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  fi

  # This is the hash on https://github.com/envoyproxy/envoy-filter-example.git we pin to.
  (cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}" && git fetch origin && git checkout -f af5aa34dc85b80646d9db12c5b901ef18cee9f45)
  sed -e "s|{ENVOY_SRCDIR}|${ENVOY_SRCDIR}|" "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example > "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/WORKSPACE
  cp -f "${ENVOY_SRCDIR}"/.bazelversion "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/.bazelversion
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
cp -f "${ENVOY_SRCDIR}"/*.bazelrc "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/

export BUILDIFIER_BIN="/usr/local/bin/buildifier"
export BUILDOZER_BIN="/usr/local/bin/buildozer"
