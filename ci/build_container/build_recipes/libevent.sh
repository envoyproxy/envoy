#!/bin/bash

set -e

VERSION=2.1.8-stable

if [[ "${OS}" == "Windows_NT" ]]; then
  wget -O libevent-"$VERSION".tar.gz https://github.com/libevent/libevent/archive/release-"$VERSION".tar.gz
  tar xf libevent-"$VERSION".tar.gz

  cd libevent-release-"$VERSION"

  mkdir build
  cd build
  cmake -G "Ninja" \
    -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
    -DEVENT__DISABLE_OPENSSL:BOOL=on \
    -DEVENT__DISABLE_REGRESS:BOOL=on \
    -DCMAKE_BUILD_TYPE=Debug \
    ..
  ninja
  ninja install
  cp "CMakeFiles/event.dir/event.pdb" "$THIRDPARTY_BUILD/lib/event.pdb"
else
  wget -O libevent-"$VERSION".tar.gz https://github.com/libevent/libevent/releases/download/release-"$VERSION"/libevent-"$VERSION".tar.gz
  tar xf libevent-"$VERSION".tar.gz

  cd libevent-"$VERSION"

  ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --disable-libevent-regress --disable-openssl
  make V=1 install
fi
