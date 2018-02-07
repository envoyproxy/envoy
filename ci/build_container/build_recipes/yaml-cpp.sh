#!/bin/bash

set -e

VERSION=0.6.1

wget -O yaml-cpp-"$VERSION".tar.gz https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-"$VERSION".tar.gz
tar xf yaml-cpp-"$VERSION".tar.gz
cd yaml-cpp-yaml-cpp-"$VERSION"
cmake -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
  -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make VERBOSE=1 install
