#!/bin/bash

set -e

VERSION=1.2.11

wget -O zlib-$VERSION.tar.gz https://github.com/madler/zlib/archive/v$VERSION.tar.gz
tar xf zlib-$VERSION.tar.gz
cd zlib-$VERSION
./configure
make
cp libz.a $THIRDPARTY_BUILD/lib
cp zlib.h $THIRDPARTY_BUILD/include