set -e

wget https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz
tar xf lightstep-tracer-cpp-0.36.tar.gz
cd lightstep-tracer-cpp-0.36
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
	    protobuf_CFLAGS="-I$THIRDPARTY_BUILD/include" protobuf_LIBS="-L$THIRDPARTY_BUILD/lib -lprotobuf" PROTOC=$THIRDPARTY_BUILD/bin/protoc
make install
