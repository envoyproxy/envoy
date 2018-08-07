#!/bin/bash

set -e
export COMMIT="e1c3a83b8197cf02e794f61228461c27d4e78cfb"  # benchmark @ Jan 11, 2018

git clone https://github.com/google/benchmark.git
(cd benchmark; git reset --hard "$COMMIT")
mkdir build

cd build
cmake -G "Ninja" ../benchmark \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
ninja

benchmark_lib="libbenchmark.a"
if [[ "${OS}" == "Windows_NT" ]]; then
  benchmark_lib="benchmark.lib"
fi

cp "src/$benchmark_lib" "$THIRDPARTY_BUILD"/lib
cd ../benchmark

INCLUDE_DIR="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$INCLUDE_DIR"
cp include/benchmark/benchmark.h "$INCLUDE_DIR"
