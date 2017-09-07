#!/bin/bash

set -e

VERSION=0.36

wget -O lightstep-tracer-cpp-"$VERSION".tar.gz https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v${VERSION//./_}/lightstep-tracer-cpp-"$VERSION".tar.gz
tar xf lightstep-tracer-cpp-"$VERSION".tar.gz
cd lightstep-tracer-cpp-"$VERSION"

# see https://github.com/lyft/envoy/issues/1387 for progress
cat > ../lightstep-missing-header.diff << EOF
--- ./src/c++11/lightstep/options.h.bak	2017-08-04 09:30:19.527076744 -0400
+++ ./src/c++11/lightstep/options.h	2017-08-04 09:30:33.742106924 -0400
@@ -5,6 +5,7 @@
 // Options for Tracer implementations, starting Spans, and finishing
 // Spans.
 
+#include <functional>
 #include <chrono>
 #include <memory>
 
EOF
patch -p0 < ../lightstep-missing-header.diff

# Added for legacy compatibility, should not be needed in new build recipes.
[ -z "$PROTOBUF_BUILD" ] && PROTOBUF_BUILD="$THIRDPARTY_BUILD"
./configure --disable-grpc --prefix="$THIRDPARTY_BUILD" --enable-shared=no \
	    protobuf_CFLAGS="-I$PROTOBUF_BUILD/include" protobuf_LIBS="-L$PROTOBUF_BUILD/lib -lprotobuf" PROTOC=$PROTOBUF_BUILD/bin/protoc
make V=1 install
