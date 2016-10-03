#!/bin/bash

set -e

export CC=gcc-4.9
export CXX=g++-4.9
NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

echo "building using $NUM_CPUS CPUs"

if [[ "$1" == "docs" ]]; then
  echo "docs build..."
  make docs
  exit 0
elif [[ "$1" == "coverage" ]]; then
  echo "coverage build..."
  EXTRA_CMAKE_FLAGS="-DENVOY_CODE_COVERAGE:BOOL=ON"
  TEST_TARGET="envoy.check-coverage"
elif [[ "$1" == "asan" ]]; then
  echo "asan build..."
  EXTRA_CMAKE_FLAGS="-DENVOY_SANITIZE:BOOL=ON"
  TEST_TARGET="envoy.check"
else
  echo "normal build..."
  TEST_TARGET="envoy.check"
fi

mkdir build
cd build

cmake \
$EXTRA_CMAKE_FLAGS -DENVOY_DEBUG:BOOL=OFF \
-DENVOY_COTIRE_MODULE_DIR:FILEPATH=/thirdparty/cotire-cotire-1.7.8/CMake \
-DENVOY_GMOCK_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_GPERFTOOLS_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_GTEST_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_HTTP_PARSER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_LIBEVENT_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_NGHTTP2_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_SPDLOG_INCLUDE_DIR:FILEPATH=/thirdparty/spdlog/include \
-DENVOY_TCLAP_INCLUDE_DIR:FILEPATH=/thirdparty/tclap-1.2.1/include \
-DENVOY_JANSSON_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_OPENSSL_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_PROTOBUF_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_PROTOBUF_PROTOC:FILEPATH=/thirdparty_build/bin/protoc \
-DENVOY_GCOVR:FILEPATH=/thirdparty/gcovr-3.3/scripts/gcovr \
-DENVOY_GCOVR_EXTRA_ARGS:STRING="-e test/* -e build/*" \
-DENVOY_EXE_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
-DENVOY_TEST_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
..

make check_format
make -j$NUM_CPUS $TEST_TARGET
