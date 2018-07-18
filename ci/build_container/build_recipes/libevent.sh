#!/bin/bash

set -e

VERSION=2.1.8-stable

wget -O libevent-release-"$VERSION".tar.gz https://github.com/libevent/libevent/archive/release-"$VERSION".tar.gz
tar xf libevent-release-"$VERSION".tar.gz
cd libevent-release-"$VERSION"
mkdir build
cd build
cmake -G "Ninja" \
  -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DEVENT__DISABLE_OPENSSL:BOOL=on \
  -DEVENT__DISABLE_REGRESS:BOOL=on \
  ..
ninja
ninja install
