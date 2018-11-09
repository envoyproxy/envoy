#!/bin/bash

set -e

VERSION=1.2.11
SHA256=629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff

curl https://github.com/madler/zlib/archive/v"$VERSION".tar.gz -sLo zlib-"$VERSION".tar.gz \
  && echo "$SHA256" zlib-"$VERSION".tar.gz | sha256sum --check
tar xf zlib-"$VERSION".tar.gz
cd zlib-"$VERSION"

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

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # .pdb files are not generated for release builds
  cp "CMakeFiles/zlibstatic.dir/zlibstatic.pdb" "$THIRDPARTY_BUILD/lib/zlibstatic.pdb"
fi
