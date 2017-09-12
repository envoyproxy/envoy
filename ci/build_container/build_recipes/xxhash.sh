#!/bin/bash

set -e

VERSION=0.6.3

wget -O xxHash-v"$VERSION".tar.gz https://github.com/Cyan4973/xxHash/archive/v"$VERSION".tar.gz
tar xf xxHash-v"$VERSION".tar.gz
cd xxHash-"$VERSION"
"$CC" $CFLAGS "$CPPFLAGS" -c xxhash.c -o xxhash.o
ar rcs xxhash.a xxhash.o
cp xxhash.a "$THIRDPARTY_BUILD"/lib
cp xxhash.h "$THIRDPARTY_BUILD"/include
