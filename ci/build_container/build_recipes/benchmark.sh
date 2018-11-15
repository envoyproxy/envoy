#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

curl "$BENCHMARK_FILE_URL" -sLo benchmark-"$BENCHMARK_GIT_SHA".tar.gz \
  && echo "$BENCHMARK_FILE_SHA256" benchmark-"$BENCHMARK_GIT_SHA".tar.gz | sha256sum --check
tar xf benchmark-"$BENCHMARK_GIT_SHA".tar.gz
cd benchmark-"$BENCHMARK_GIT_SHA"

mkdir build
cd build

cmake -G "Ninja" \
  -DCMAKE_BUILD_TYPE=RELEASE \
  -DBENCHMARK_ENABLE_GTEST_TESTS=OFF \
  ..
ninja

benchmark_lib="libbenchmark.a"
if [[ "${OS}" == "Windows_NT" ]]; then
  benchmark_lib="benchmark.lib"
fi

cp "src/$benchmark_lib" "$THIRDPARTY_BUILD"/lib
cd ..

INCLUDE_DIR="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$INCLUDE_DIR"
cp include/benchmark/benchmark.h "$INCLUDE_DIR"
