#!/bin/bash

set -e

# use commit where cmake 3.6 feature removed. Unblocks Ubuntu 16.xx or below builds
# TODO (moderation) change back to tarball method on next benchmark release
export COMMIT="505be96ab23056580a3a2315abba048f4428b04e"

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
