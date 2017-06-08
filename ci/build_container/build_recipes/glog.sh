#!/bin/bash

set -e

VERSION=0.3.5

wget -O glog-$VERSION.tar.gz https://github.com/google/glog/archive/v$VERSION.tar.gz
tar xf glog-$VERSION.tar.gz
cd glog-$VERSION
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make VERBOSE=1 install
