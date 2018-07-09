#!/bin/bash

set -e

VERSION=2.7
if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

curl https://github.com/gperftools/gperftools/releases/download/gperftools-"$VERSION"/gperftools-"$VERSION".tar.gz -sLo gperftools-"$VERSION".tar.gz
tar xf gperftools-"$VERSION".tar.gz
cd gperftools-"$VERSION"

LDFLAGS="-lpthread" ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind
make V=1 install
