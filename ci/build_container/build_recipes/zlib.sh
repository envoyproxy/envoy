#!/bin/bash

set -e

VERSION=1.2.11

curl https://github.com/madler/zlib/archive/v"$VERSION".tar.gz -sLo zlib-"$VERSION".tar.gz
tar xf zlib-"$VERSION".tar.gz
cd zlib-"$VERSION"
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/zlibstatic.dir/zlibstatic.pdb" "$THIRDPARTY_BUILD/lib/zlibstatic.pdb"
fi
