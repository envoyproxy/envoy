#!/bin/bash

set -e

VERSION=43863938377a9ea1399c0596269e0890b5c5515a

wget -O googletest-"$VERSION".tar.gz https://github.com/google/googletest/archive/"$VERSION".tar.gz
tar xf googletest-"$VERSION".tar.gz
cd googletest-"$VERSION"
cmake -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
  -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make VERBOSE=1 install
