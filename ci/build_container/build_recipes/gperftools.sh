#!/bin/bash

set -e

VERSION=2.6.1

wget -O gperftools-"$VERSION".tar.gz https://github.com/gperftools/gperftools/releases/download/gperftools-"$VERSION"/gperftools-"$VERSION".tar.gz
tar xf gperftools-"$VERSION".tar.gz
cd gperftools-"$VERSION"

# TODO(zuercher): Remove this workaround for https://github.com/gperftools/gperftools/issues/910
if [[ `uname` == "Darwin" ]];
then
  export CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=500 -D_DARWIN_C_SOURCE"
fi

LDFLAGS="-lpthread" ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind
make V=1 install
