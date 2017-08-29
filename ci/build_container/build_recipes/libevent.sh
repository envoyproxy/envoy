#!/bin/bash

set -e

VERSION=2.1.8-stable

wget -O libevent-"$VERSION".tar.gz https://github.com/libevent/libevent/releases/download/release-"$VERSION"/libevent-"$VERSION".tar.gz
tar xf libevent-"$VERSION".tar.gz
cd libevent-"$VERSION"
./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --disable-libevent-regress --disable-openssl
make V=1 install
