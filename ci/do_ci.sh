#!/bin/bash

set -e

export CC=gcc-4.9
export CXX=g++-4.9
export HEAPCHECK=normal

NUM_CPUS=`grep -c ^processor /proc/cpuinfo`
echo "building using $NUM_CPUS CPUs"

if [[ "$1" == "docs" ]]; then
  echo "docs build..."
  make docs
  exit 0
elif [[ "$1" == "coverage" ]]; then
  echo "coverage build with tests..."
  EXTRA_CMAKE_FLAGS="-DENVOY_CODE_COVERAGE:BOOL=ON"
  TEST_TARGET="envoy.check-coverage"
elif [[ "$1" == "asan" ]]; then
  echo "asan build with tests..."
  EXTRA_CMAKE_FLAGS="-DENVOY_SANITIZE:BOOL=ON -DENVOY_DEBUG:BOOL=OFF"
  TEST_TARGET="envoy.check"
elif [[ "$1" == "debug" ]]; then
  echo "debug build with tests..."
  EXTRA_CMAKE_FLAGS="-DENVOY_DEBUG:BOOL=ON"
  TEST_TARGET="envoy.check"
elif [[ "$1" == "server_only" ]]; then
  echo "normal build server only..."
  EXTRA_CMAKE_FLAGS="-DENVOY_DEBUG:BOOL=OFF -DENVOY_STRIP:BOOL=ON"
  TEST_TARGET="envoy"
else
  echo "normal build with tests..."
  EXTRA_CMAKE_FLAGS="-DENVOY_DEBUG:BOOL=OFF"
  TEST_TARGET="envoy.check"
fi

mkdir -p build
cd build

cmake \
$EXTRA_CMAKE_FLAGS \
-DENVOY_COTIRE_MODULE_DIR:FILEPATH=/thirdparty/cotire-cotire-1.7.8/CMake \
-DENVOY_GMOCK_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_GPERFTOOLS_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_GTEST_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_HTTP_PARSER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_LIBEVENT_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
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

make check_format
make -j$NUM_CPUS $TEST_TARGET
