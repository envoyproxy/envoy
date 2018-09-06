#!/bin/bash

set -e

VERSION=1.4.1
SHA256=f8e525db3c42efc9c7f3bc5176a8fa893a9a9920bbd08cef30fb56a51854d60d

curl https://github.com/google/benchmark/archive/v"$VERSION".tar.gz -sLo benchmark-"$VERSION".tar.gz \
  && echo "$SHA256" benchmark-"$VERSION".tar.gz | sha256sum --check
tar xf benchmark-"$VERSION".tar.gz
cd benchmark-"$VERSION"

mkdir build
cd build

cmake -G "Ninja" ../ \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
ninja

benchmark_lib="libbenchmark.a"
if [[ "${OS}" == "Windows_NT" ]]; then
  benchmark_lib="benchmark.lib"
fi

cp "src/$benchmark_lib" "$THIRDPARTY_BUILD"/lib
cd ../

INCLUDE_DIR="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$INCLUDE_DIR"
cp include/benchmark/benchmark.h "$INCLUDE_DIR"
