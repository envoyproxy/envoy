#!/bin/bash

set -e

# use commit where cmake 3.6 feature removed. Unblocks Ubuntu 16.xx or below builds
# TODO (moderation) change back to tarball method on next benchmark release
export COMMIT="505be96ab23056580a3a2315abba048f4428b04e"

git clone https://github.com/google/benchmark.git
(cd benchmark; git reset --hard "$COMMIT")
mkdir build

cd build

build_type=Release
if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library -- there are different versions for debug and
  # release builds. The script "ci/do_ci.ps1" will pass BAZEL_WINDOWS_BUILD_TYPE=dbg
  # to bazel when performing a debug build.
  build_type=Debug
fi

cmake -G "Ninja" ../benchmark \
  -DCMAKE_BUILD_TYPE="$build_type" \
  -DBENCHMARK_ENABLE_GTEST_TESTS=OFF
ninja

benchmark_lib="libbenchmark.a"
if [[ "${OS}" == "Windows_NT" ]]; then
  benchmark_lib="benchmark.lib"
  if [[ "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
    # .pdb files are not generated for release builds
    cp "src/CMakeFiles/benchmark.dir/benchmark.pdb" "$THIRDPARTY_BUILD/lib/benchmark.pdb"
  fi
fi

cp "src/$benchmark_lib" "$THIRDPARTY_BUILD"/lib
cd ../benchmark

INCLUDE_DIR="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$INCLUDE_DIR"
cp include/benchmark/benchmark.h "$INCLUDE_DIR"
