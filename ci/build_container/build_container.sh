#!/bin/bash

. ./versions.bzl

set -e

# Setup basic requirements and install them.
apt-get update
apt-get install -y wget software-properties-common make cmake git python python-pip clang-format-3.6 bc libtool automake unzip
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

# buildifier
export GOPATH=/usr/lib/go
go get github.com/bazelbuild/buildifier/buildifier

# Build artifacts
THIRDPARTY_BUILD=/thirdparty_build
mkdir ${THIRDPARTY_BUILD}

# Source artifacts
mkdir thirdparty
cd thirdparty

# GCC 4.9 for everything.
export CC=gcc-4.9
export CXX=g++-4.9

# libevent
wget ${LIBEVENT_HTTP_ARCHIVE}
tar xf libevent-2.1.8-stable.tar.gz
cd ${LIBEVENT_PREFIX}
./configure --prefix=${THIRDPARTY_BUILD} --enable-shared=no --disable-libevent-regress --disable-openssl
make install
cd ..
rm -fr libevent*

# BoringSSL
git clone ${BORINGSSL_REMOTE}
cd boringssl
git reset --hard ${BORINGSSL_COMMIT}
bazel build -c opt --strategy=CppCompile=standalone --strategy=CppLink=standalone //...
cp -r src/include/* ${THIRDPARTY_BUILD}/include
cp bazel-bin/libssl.a ${THIRDPARTY_BUILD}/lib
cp bazel-bin/libcrypto.a ${THIRDPARTY_BUILD}/lib
cd ..
rm -rf boringssl

# gperftools
wget ${GPERFTOOLS_HTTP_ARCHIVE}
tar xf gperftools-2.5.tar.gz
cd ${GPERFTOOLS_PREFIX}
./configure --prefix=${THIRDPARTY_BUILD} --enable-shared=no --enable-frame-pointers
make install
cd ..
rm -fr gperftools*

# nghttp2
wget ${NGHTTP2_HTTP_ARCHIVE}
tar xf nghttp2-1.20.0.tar.gz
cd ${NGHTTP2_PREFIX}
./configure --prefix=${THIRDPARTY_BUILD} --enable-shared=no --enable-lib-only
make install
cd ..
rm -fr nghttp2*

# c-ares
git clone ${CARES_REMOTE}
cd c-ares
git reset --hard ${CARES_COMMIT}
./buildconf
./configure --prefix=${THIRDPARTY_BUILD} --enable-shared=no --enable-lib-only
make install
cd ..
rm -fr c-ares*

# protobuf
git clone ${PROTOBUF_REMOTE}
cd protobuf
git reset --hard ${PROTOBUF_COMMIT}
./autogen.sh
./configure --prefix=${THIRDPARTY_BUILD} --enable-shared=no
make install
make distclean
cd ..

# cotire
wget https://github.com/sakra/cotire/archive/cotire-1.7.8.tar.gz
tar xf cotire-1.7.8.tar.gz
rm cotire-1.7.8.tar.gz

# spdlog
git clone ${SPDLOG_REMOTE}
cd spdlog
git reset --hard ${SPDLOG_COMMIT}
cd ..

# http-parser
git clone ${HTTP_PARSER_REMOTE}
cd http-parser
git reset --hard ${HTTP_PARSER_COMMIT}
$CC -O2 -c http_parser.c -o http_parser.o
ar rcs libhttp_parser.a http_parser.o
cp libhttp_parser.a ${THIRDPARTY_BUILD}/lib
cp http_parser.h ${THIRDPARTY_BUILD}/include
cd ..
rm -fr http-parser*

# tclap
wget ${TCLAP_HTTP_ARCHIVE}
tar xf tclap-1.2.1.tar.gz
rm tclap-1.2.1.tar.gz

# lightstep
wget ${LIGHTSTEP_HTTP_ARCHIVE}
tar xf lightstep-tracer-cpp-0.36.tar.gz
cd ${LIGHTSTEP_PREFIX}
./configure --disable-grpc --prefix=${THIRDPARTY_BUILD} --enable-shared=no \
	    protobuf_CFLAGS="-I${THIRDPARTY_BUILD}/include" protobuf_LIBS="-L${THIRDPARTY_BUILD}/lib -lprotobuf" PROTOC=${THIRDPARTY_BUILD}/bin/protoc
make install
cd ..
rm -rf lightstep-tracer*

# rapidjson
git clone ${RAPIDJSON_REMOTE}
cd rapidjson
git reset --hard ${RAPIDJSON_COMMIT}
cd ..

# googletest
git clone ${GOOGLETEST_REMOTE}
cd googletest
git reset --hard ${GOOGLETEST_COMMIT}
cmake -DCMAKE_INSTALL_PREFIX:PATH=${THIRDPARTY_BUILD} .
make install
cd ..
rm -fr googletest*

# gcovr
wget -O gcovr-3.3.tar.gz ${GCOVR_HTTP_ARCHIVE}
tar xf gcovr-3.3.tar.gz
rm gcovr-3.3.tar.gz
