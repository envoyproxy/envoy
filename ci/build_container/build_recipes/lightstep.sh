set -e

wget https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz
tar xf lightstep-tracer-cpp-0.36.tar.gz
cd lightstep-tracer-cpp-0.36
# Added for legacy compatibility, should not be needed in new build recipes.
[ -z "$PROTOBUF_BUILD" ] && PROTOBUF_BUILD="$THIRDPARTY_BUILD"
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
	    protobuf_CFLAGS="-I$PROTOBUF_BUILD/include" protobuf_LIBS="-L$PROTOBUF_BUILD/lib -lprotobuf" PROTOC=$PROTOBUF_BUILD/bin/protoc
make install
