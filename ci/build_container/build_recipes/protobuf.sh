#!/bin/bash

set -e

VERSION=3.2.0

wget -O protobuf-$VERSION.tar.gz https://github.com/google/protobuf/releases/download/v$VERSION/protobuf-cpp-$VERSION.tar.gz
tar xf protobuf-$VERSION.tar.gz
rsync -av protobuf-$VERSION/* $THIRDPARTY_SRC/protobuf
cd protobuf-$VERSION
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make V=1 install

# These internal headers are needed by the GRPC-JSON transcoding library.
# https://github.com/grpc-ecosystem/grpc-httpjson-transcoding
# TODO(htuch): Clean up protobuf deps builds with envoy-api
rsync -av --include '*/' --include '*.h' --exclude '*' src/google/protobuf/util/internal $THIRDPARTY_BUILD/include/google/protobuf/util/
cp src/google/protobuf/stubs/strutil.h $THIRDPARTY_BUILD/include/google/protobuf/stubs/
cp src/google/protobuf/stubs/statusor.h $THIRDPARTY_BUILD/include/google/protobuf/stubs/
