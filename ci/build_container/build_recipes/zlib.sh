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

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/zlibstatic.dir/zlibstatic.pdb" "$THIRDPARTY_BUILD/lib/zlibstatic.pdb"
fi
