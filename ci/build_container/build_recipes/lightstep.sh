#!/bin/bash

set -e

VERSION=0.36

wget -O lightstep-tracer-cpp-$VERSION.tar.gz https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v${VERSION//./_}/lightstep-tracer-cpp-$VERSION.tar.gz
tar xf lightstep-tracer-cpp-$VERSION.tar.gz
cd lightstep-tracer-cpp-$VERSION
# Added for legacy compatibility, should not be needed in new build recipes.
[ -z "$PROTOBUF_BUILD" ] && PROTOBUF_BUILD="$THIRDPARTY_BUILD"
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
	    protobuf_CFLAGS="-I$PROTOBUF_BUILD/include" protobuf_LIBS="-L$PROTOBUF_BUILD/lib -lprotobuf" PROTOC=$PROTOBUF_BUILD/bin/protoc
make install
