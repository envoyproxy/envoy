#!/bin/bash

set -e

VERSION=2.1.8-stable
SHA256=316ddb401745ac5d222d7c529ef1eada12f58f6376a66c1118eee803cb70f83d

# Maintainer provided source tarball does not contain cmake content so using Github tarball.
curl https://github.com/libevent/libevent/archive/release-"$VERSION".tar.gz -sLo libevent-release-"$VERSION".tar.gz \
  && echo "$SHA256" libevent-release-"$VERSION".tar.gz | sha256sum --check
tar xf libevent-release-"$VERSION".tar.gz
cd libevent-release-"$VERSION"

mkdir build
cd build

# libevent defaults CMAKE_BUILD_TYPE to Release
build_type=Release
if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library -- there are different versions for debug and
  # release builds. The script "ci/do_ci.ps1" will pass BAZEL_WINDOWS_BUILD_TYPE=dbg
  # to bazel when performing a debug build.
  build_type=Debug
fi

cmake -G "Ninja" \
  -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DEVENT__DISABLE_OPENSSL:BOOL=on \
  -DEVENT__DISABLE_REGRESS:BOOL=on \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # .pdb files are not generated for release builds
  cp "CMakeFiles/event.dir/event.pdb" "$THIRDPARTY_BUILD/lib/event.pdb"
fi
