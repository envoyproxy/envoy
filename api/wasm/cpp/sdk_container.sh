#!/bin/bash

set -e

# basics
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get upgrade -y
apt-get install -y --no-install-recommends apt-utils ca-certificates
apt-get autoremove -y
apt-get clean
apt-get install -y --no-install-recommends software-properties-common apt-transport-https git wget curl pkg-config autoconf autotools-dev automake libtool cmake python zlib1g-dev

# gcc-7
apt-get install -y --no-install-recommends gcc-7 g++-7 cpp-7
export CC=gcc-7
export CXX=g++-7
export CPP=cpp-7

# get $HOME
cd

# specific version of protobufs to match the pre-compiled support libraries
git clone https://github.com/protocolbuffers/protobuf
cd protobuf
git checkout v3.9.1
git submodule update --init --recursive
./autogen.sh
./configure
make
make check
make install
cd
rm -rf protobuf

# emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk update-tags
./emsdk install 1.39.2
./emsdk activate 1.39.2
source ./emsdk_env.sh
cd

# abseil (optional)
git clone https://github.com/abseil/abseil-cpp
cd abseil-cpp
git checkout 14550beb3b7b97195e483fb74b5efb906395c31e -b Jul302019 # Jul 30 2019
emconfigure cmake -DCMAKE_CXX_STANDARD=17 "."
emmake make
cd

# WAVM (optional)
apt-get install -y --no-install-recommends llvm-6.0-dev
git clone https://github.com/WAVM/WAVM
cd WAVM
git checkout 1ec06cd202a922015c9041c5ed84f875453c4dc7 -b Oct152019 # Oct 15 2019
cmake "."
make
make install
cd
rm -rf WAVM
