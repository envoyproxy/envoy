#!/bin/bash

set -e

export CC=gcc-4.9
export CXX=g++-4.9
NUM_CPUS=`grep -c ^processor /proc/cpuinfo`

echo "building using $NUM_CPUS CPUs"

mkdir build
cd build

cmake \
$EXTRA_CMAKE_FLAGS -DENVOY_DEBUG:BOOL=OFF \
-DENVOY_COTIRE_MODULE_DIR:FILEPATH=/thirdparty/cotire-cotire-1.7.8/CMake \
-DENVOY_HTTP_PARSER_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_LIBEVENT_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_NGHTTP2_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_SPDLOG_INCLUDE_DIR:FILEPATH=/thirdparty/spdlog/include \
-DENVOY_TCLAP_INCLUDE_DIR:FILEPATH=/thirdparty/tclap-1.2.1/include \
-DENVOY_JANSSON_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_OPENSSL_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_PROTOBUF_INCLUDE_DIR:FILEPATH=/thirdparty_build/include \
-DENVOY_PROTOBUF_PROTOC:FILEPATH=/thirdparty_build/bin/protoc \
-DENVOY_EXE_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
-DENVOY_TEST_EXTRA_LINKER_FLAGS:STRING=-L/thirdparty_build/lib \
..

make check_format

make
