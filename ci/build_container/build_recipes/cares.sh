#!/bin/bash

set -e

VERSION=cares-1_12_0

wget -O c-ares-$VERSION.tar.gz https://github.com/c-ares/c-ares/archive/$VERSION.tar.gz
tar xf c-ares-$VERSION.tar.gz
cd c-ares-$VERSION
./buildconf
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
cd ..
rm -rf c-ares-$VERSION*
