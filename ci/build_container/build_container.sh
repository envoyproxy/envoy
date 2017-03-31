#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip clang-format-3.6 bc libtool automake
apt-get install -y golang
# For debugging.
apt-get install -y gdb strace
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y g++-4.9
# Bazel and related dependencies.
apt-get install -y openjdk-8-jdk curl
echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
apt-get update
apt-get install -y bazel
rm -rf /var/lib/apt/lists/*

# virtualenv
pip install virtualenv

# Build artifacts
THIRDPARTY_BUILD=/thirdparty_build
mkdir $THIRDPARTY_BUILD

# Source artifacts
mkdir thirdparty
cd thirdparty

# GCC 4.9 for everything.
export CC=gcc-4.9
export CXX=g++-4.9

# libevent
wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
tar xf libevent-2.1.8-stable.tar.gz
cd libevent-2.1.8-stable
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --disable-libevent-regress --disable-openssl
make install
cd ..
rm -fr libevent*

# BoringSSL
git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard b87c80300647c2c0311c1489a104470e099f1531
cmake .
make
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
cd ..
rm -rf boringssl

# gperftools
wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.5/gperftools-2.5.tar.gz
tar xf gperftools-2.5.tar.gz
cd gperftools-2.5
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-frame-pointers
make install
cd ..
rm -fr gperftools*

# nghttp2
wget https://github.com/nghttp2/nghttp2/releases/download/v1.20.0/nghttp2-1.20.0.tar.gz
tar xf nghttp2-1.20.0.tar.gz
cd nghttp2-1.20.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
cd ..
rm -fr nghttp2*

# c-ares
wget https://github.com/c-ares/c-ares/archive/cares-1_12_0.tar.gz
tar xf cares-1_12_0.tar.gz
cd c-ares-cares-1_12_0
./buildconf
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
cd ..
rm -fr cares* c-ares*

# protobuf
wget https://github.com/google/protobuf/releases/download/v3.2.0/protobuf-cpp-3.2.0.tar.gz
tar xf protobuf-cpp-3.2.0.tar.gz
cd protobuf-3.2.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
cd ..

# cotire
wget https://github.com/sakra/cotire/archive/cotire-1.7.8.tar.gz
tar xf cotire-1.7.8.tar.gz
rm cotire-1.7.8.tar.gz

# spdlog
wget https://github.com/gabime/spdlog/archive/v0.11.0.tar.gz
tar xf v0.11.0.tar.gz
rm v0.11.0.tar.gz

# http-parser
wget -O http-parser-v2.7.0.tar.gz https://github.com/nodejs/http-parser/archive/v2.7.0.tar.gz
tar xf http-parser-v2.7.0.tar.gz
cd http-parser-2.7.0
$CC -O2 -c http_parser.c -o http_parser.o
ar rcs libhttp_parser.a http_parser.o
cp libhttp_parser.a $THIRDPARTY_BUILD/lib
cp http_parser.h $THIRDPARTY_BUILD/include
cd ..
rm -fr http-parser*

# tclap
wget -O tclap-1.2.1.tar.gz https://sourceforge.net/projects/tclap/files/tclap-1.2.1.tar.gz/download
tar xf tclap-1.2.1.tar.gz
rm tclap-1.2.1.tar.gz

# lightstep
wget https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz
tar xf lightstep-tracer-cpp-0.36.tar.gz
cd lightstep-tracer-cpp-0.36
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
	    protobuf_CFLAGS="-I$THIRDPARTY_BUILD/include" protobuf_LIBS="-L$THIRDPARTY_BUILD/lib -lprotobuf" PROTOC=$THIRDPARTY_BUILD/bin/protoc
make install
cd ..
rm -rf lightstep-tracer*

# rapidjson
wget -O rapidjson-1.1.0.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
tar xf rapidjson-1.1.0.tar.gz
rm rapidjson-1.1.0.tar.gz

# googletest
wget -O googletest-1.8.0.tar.gz https://github.com/google/googletest/archive/release-1.8.0.tar.gz
tar xf googletest-1.8.0.tar.gz
cd googletest-release-1.8.0
cmake -DCMAKE_INSTALL_PREFIX:PATH=$THIRDPARTY_BUILD .
make install
cd ..
rm -fr googletest*

# gcovr
wget -O gcovr-3.3.tar.gz https://github.com/gcovr/gcovr/archive/3.3.tar.gz
tar xf gcovr-3.3.tar.gz
rm gcovr-3.3.tar.gz
