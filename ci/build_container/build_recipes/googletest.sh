#!/bin/bash

set -e

VERSION=release-1.8.0

wget -O googletest-$VERSION.tar.gz https://github.com/google/googletest/archive/$VERSION.tar.gz
tar xf googletest-$VERSION.tar.gz
cd googletest-$VERSION
cmake -DCMAKE_INSTALL_PREFIX:PATH=$THIRDPARTY_BUILD .
make install
cd ..
rm -rf googletest-$VERSION*
