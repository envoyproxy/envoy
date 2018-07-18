#!/bin/bash

set -e

VERSION=1.2.11

wget -O zlib-"$VERSION".tar.gz https://github.com/madler/zlib/archive/v"$VERSION".tar.gz
tar xf zlib-"$VERSION".tar.gz
cd zlib-"$VERSION"
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" ..
ninja
ninja install
