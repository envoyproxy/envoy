#!/bin/bash

set -e
export COMMIT="e1c3a83b8197cf02e794f61228461c27d4e78cfb"  # benchmark @ Jan 11, 2018

git clone https://github.com/google/benchmark.git
(cd benchmark; git reset --hard "$COMMIT")
mkdir build
cd build

cmake_generator="Unix Makefiles"
make_cmd=make
benchmark_lib="libbenchmark.a"

if [[ "${OS}" == "Windows_NT" ]]; then
  cmake_generator="Ninja"
  make_cmd=ninja
  benchmark_lib="benchmark.lib"
fi

cmake -G "$cmake_generator" ../benchmark \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
"$make_cmd"
cp "src/$benchmark_lib" "$THIRDPARTY_BUILD"/lib
cd ../benchmark

INCLUDE_DIR="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$INCLUDE_DIR"
cp include/benchmark/benchmark.h "$INCLUDE_DIR"
