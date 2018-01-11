#!/bin/bash

set -x

export COMMIT="e1c3a83b8197cf02e794f61228461c27d4e78cfb"  # benchmark @ Jan 11, 2018

git clone https://github.com/google/benchmark.git
(cd benchmark; git reset --hard "$COMMIT")
git clone https://github.com/google/googletest.git benchmark/googletest
mkdir build

cd build
cmake -G "Unix Makefiles" ../benchmark -DCMAKE_BUILD_TYPE=RELEASE
make
cp src/libbenchmark.a "$THIRDPARTY_BUILD"/lib
cd ../benchmark

pwd
include_dir="$THIRDPARTY_BUILD/include/testing/base/public"
mkdir -p "$include_dir"
cp include/benchmark/benchmark.h "$include_dir"
