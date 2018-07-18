#!/bin/bash

set -e

# Use master branch, which contains a fix for the spurious limit of 100 concurrent streams:
# https://github.com/nghttp2/nghttp2/commit/2ba1389993729fcb6ee5794ac512f2b67b29952e
# TODO(PiotrSikora): switch back to releases once v1.33.0 is out.
VERSION=e5b3f9addd49bca27e2f99c5c65a564eb5c0cf6d  # 2018-06-09

wget -O nghttp2-"$VERSION".tar.gz https://github.com/nghttp2/nghttp2/archive/"$VERSION".tar.gz
tar xf nghttp2-"$VERSION".tar.gz
cd nghttp2-"$VERSION"
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DENABLE_STATIC_LIB=on \
  -DENABLE_LIB_ONLY=on \
  ..
ninja
ninja install
