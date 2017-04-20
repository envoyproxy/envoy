#!/bin/bash

# Configure environment variables, generate makefiles and switch to build
# directory in preparation for an invocation of the generated build makefiles.

set -e

export CC=gcc-4.9
export CXX=g++-4.9
export HEAPCHECK=normal
export PPROF_PATH=/thirdparty_build/bin/pprof

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

export ENVOY_SRCDIR=/source

if [[ "$1" == bazel* ]]
then
  export BUILD_DIR=/build
  # Make sure that "docker run" has a -v bind mount for /build, since cmake
  # users will only have a bind mount for /source.
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
    --verbose_failures ${BAZEL_OPTIONS}"
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
else
  # TODO(htuch): Remove everything below this comment when we turn off cmake.
  if [[ "$1" == "coverage" ]]; then
    EXTRA_CMAKE_FLAGS+=" -DENVOY_CODE_COVERAGE:BOOL=ON"
  elif [[ "$1" == "asan" ]]; then
    EXTRA_CMAKE_FLAGS+=" -DENVOY_SANITIZE:BOOL=ON -DENVOY_DEBUG:BOOL=OFF"
  elif [[ "$1" == "debug" ]]; then
    EXTRA_CMAKE_FLAGS+=" -DENVOY_DEBUG:BOOL=ON"
  elif [[ "$1" == "server_only" ]]; then
    EXTRA_CMAKE_FLAGS+=" -DENVOY_DEBUG:BOOL=OFF -DENVOY_STRIP:BOOL=ON"
  else
    EXTRA_CMAKE_FLAGS+=" -DENVOY_DEBUG:BOOL=OFF"
  fi

  mkdir -p build_"$1"
  cd build_"$1"

  cmake \
  ${EXTRA_CMAKE_FLAGS} \
  -DENVOY_COTIRE_MODULE_DIR:FILEPATH=/thirdparty/cotire-cotire-1.7.8/CMake \
  -DENVOY_GMOCK_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_GPERFTOOLS_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_GTEST_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_HTTP_PARSER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_LIBEVENT_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_CARES_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_NGHTTP2_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_SPDLOG_INCLUDE_DIR:FILEPATH=/thirdparty/spdlog-0.11.0/include \
  -DENVOY_TCLAP_INCLUDE_DIR:FILEPATH=/thirdparty/tclap-1.2.1/include \
  -DENVOY_OPENSSL_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_PROTOBUF_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
  -DENVOY_PROTOBUF_PROTOC:FILEPATH=/thirdparty_build/bin/protoc \
  -DENVOY_GCOVR:FILEPATH=/thirdparty/gcovr-3.3/scripts/gcovr \
  -DENVOY_RAPIDJSON_INCLUDE_DIR:FILEPATH=/thirdparty/rapidjson-1.1.0/include \
  -DENVOY_GCOVR_EXTRA_ARGS:STRING="-e test/* -e build/*" \
  -DENVOY_EXE_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
  -DENVOY_TEST_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
  ..

  cmake -L || true
fi
