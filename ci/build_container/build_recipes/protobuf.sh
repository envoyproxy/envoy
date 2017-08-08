#!/bin/bash

set -e

# Unless overriden with an explicit release tag, e.g. v3.2.0rc2, we use a pinned
# HEAD commit. This is only until we get a release with
# https://github.com/google/protobuf/pull/3327, i.e. v3.4.0.
[ -z "$ENVOY_PROTOBUF_COMMIT" ] && ENVOY_PROTOBUF_COMMIT=062df3d0724d9ae5e3c65d481dc1d3aca811152e  # 2017-07-20

git clone https://github.com/google/protobuf.git
rsync -av protobuf/* $THIRDPARTY_SRC/protobuf
cd protobuf
git reset --hard "$ENVOY_PROTOBUF_COMMIT"
./autogen.sh
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make V=1 install

# These internal headers are needed by the GRPC-JSON transcoding library.
# https://github.com/grpc-ecosystem/grpc-httpjson-transcoding
# TODO(htuch): Clean up protobuf deps builds with envoy-api
rsync -av src/google/protobuf/util/internal/*.h $THIRDPARTY_BUILD/include/google/protobuf/util/internal
cp src/google/protobuf/stubs/strutil.h $THIRDPARTY_BUILD/include/google/protobuf/stubs/
cp src/google/protobuf/stubs/statusor.h $THIRDPARTY_BUILD/include/google/protobuf/stubs/
