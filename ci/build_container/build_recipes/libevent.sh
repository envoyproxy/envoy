#!/bin/bash

set -e

VERSION=2.1.8-stable

curl https://github.com/libevent/libevent/archive/release-"$VERSION".tar.gz -sLo libevent-release-"$VERSION".tar.gz
tar xf libevent-release-"$VERSION".tar.gz
cd libevent-release-"$VERSION"

mkdir build
cd build

# libevent defaults CMAKE_BUILD_TYPE to Release
build_type=Release
if [[ "${OS}" == "Windows_NT" ]]; then
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

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/event.dir/event.pdb" "$THIRDPARTY_BUILD/lib/event.pdb"
fi
