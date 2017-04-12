#!/bin/bash

# Configure environment variables, generate makefiles and switch to build
# directory in preparation for an invocation of the generated build makefiles.

set -e

export CC=gcc-4.9
export CXX=g++-4.9
export HEAPCHECK=normal
export PPROF_PATH=/thirdparty_build/bin/pprof

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

if [[ "$1" == bazel* ]]
then
  export USER=bazel
  # Prefixing with bazel- is necessary to prevent creating symlink loops that
  # confuses gcovr.
  export TEST_TMPDIR=/source/bazel-build
  export SRCDIR="$(realpath "${PWD}")"
  export BAZEL="bazel"
  export BAZEL_COVERAGE="bazel-coverage"
  # Not sandboxing, since non-privileged Docker can't do nested namespaces.
  export BAZEL_BUILD_OPTIONS="--strategy=CppCompile=standalone --strategy=CppLink=standalone \
    --strategy=TestRunner=standalone --strategy=ProtoCompile=standalone \
    --strategy=Genrule=standalone --verbose_failures --package_path %workspace%:.."
  [[ "${BAZEL_EXPUNGE}" == "1" ]] && "${BAZEL}" clean --expunge
  mkdir -p "${TEST_TMPDIR}"
  cp ci/WORKSPACE "${TEST_TMPDIR}"
  ln -sf /thirdparty ci/prebuilt
  ln -sf /thirdparty_build ci/prebuilt
  cd "${TEST_TMPDIR}"
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
