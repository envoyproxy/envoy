#!/bin/bash

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip clang-format-3.6 bc
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y g++-4.9
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

# openssl
wget https://www.openssl.org/source/openssl-1.0.2h.tar.gz
tar xf openssl-1.0.2h.tar.gz
cd openssl-1.0.2h
./config --prefix=$THIRDPARTY_BUILD -DPURIFY no-shared
make install
cd ..
rm -fr openssl*

# libevent
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar xf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --disable-libevent-regress \
    CPPFLAGS=-I$THIRDPARTY_BUILD/include LDFLAGS=-L$THIRDPARTY_BUILD/lib \
    OPENSSL_LIBADD=-ldl
make install
cd ..
rm -fr libevent*

# gperftools
wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.5/gperftools-2.5.tar.gz
tar xf gperftools-2.5.tar.gz
cd gperftools-2.5
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-frame-pointers
make install
cd ..
rm -fr gperftools*

# jansson
wget http://www.digip.org/jansson/releases/jansson-2.7.tar.gz
tar xf jansson-2.7.tar.gz
cd jansson-2.7
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
cd ..
rm -fr jansson*

# nghttp2
wget https://github.com/nghttp2/nghttp2/releases/download/v1.14.0/nghttp2-1.14.0.tar.gz
tar xf nghttp2-1.14.0.tar.gz
cd nghttp2-1.14.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
cd ..
rm -fr nghttp2*

# protobuf
wget https://github.com/google/protobuf/releases/download/v3.0.0/protobuf-cpp-3.0.0.tar.gz
tar xf protobuf-cpp-3.0.0.tar.gz
cd protobuf-3.0.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
cd ..
rm -fr protobuf*

# cotire
wget https://github.com/sakra/cotire/archive/cotire-1.7.8.tar.gz
tar xf cotire-1.7.8.tar.gz
rm cotire-1.7.8.tar.gz

# spdlog (not tag based)
git clone https://github.com/gabime/spdlog.git
cd spdlog
git reset --hard 319a62
cd ..

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
wget https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_14/lightstep-tracer-cpp-0.14.tar.gz
tar xf lightstep-tracer-cpp-0.14.tar.gz
cd lightstep-tracer-cpp-0.14
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
	    protobuf_CFLAGS="-I$THIRDPARTY_BUILD/include" protobuf_LIBS="-L$THIRDPARTY_BUILD/lib -lprotobuf"
make install
rm -rf lightstep-tracer-cpp-0.14
cd ..

# googletest
wget -O googletest-1.8.0.tar.gz https://github.com/google/googletest/archive/release-1.8.0.tar.gz
tar xf googletest-1.8.0.tar.gz
cd googletest-release-1.8.0
cmake -DCMAKE_INSTALL_PREFIX:PATH=$THIRDPARTY_BUILD .
make install
cd ..
rm -fr googletest

# gcovr
wget -O gcovr-3.3.tar.gz https://github.com/gcovr/gcovr/archive/3.3.tar.gz
tar xf gcovr-3.3.tar.gz
rm gcovr-3.3.tar.gz
