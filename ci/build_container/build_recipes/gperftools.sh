#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

VERSION=2.7
SHA256=1ee8c8699a0eff6b6a203e59b43330536b22bbcbe6448f54c7091e5efb0763c9

curl https://github.com/gperftools/gperftools/releases/download/gperftools-"$VERSION"/gperftools-"$VERSION".tar.gz -sLo gperftools-"$VERSION".tar.gz \
  && echo "$SHA256 " gperftools-"$VERSION".tar.gz | shasum -a 256 --check
tar xf gperftools-"$VERSION".tar.gz
cd gperftools-"$VERSION"

LDFLAGS="-lpthread" ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind
make V=1 install
