#!/bin/bash

set -e

VERSION=3.2.0

wget -O protobuf-$VERSION.tar.gz https://github.com/google/protobuf/releases/download/v$VERSION/protobuf-cpp-$VERSION.tar.gz
tar xf protobuf-$VERSION.tar.gz
rsync -av protobuf-$VERSION $THIRDPARTY_SRC
cd protobuf-$VERSION
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
