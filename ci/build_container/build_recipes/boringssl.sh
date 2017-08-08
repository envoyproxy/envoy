#!/bin/bash

set -e

COMMIT=68f84f5c40644e029ed066999448696b01caba7a  # chromium-60.0.3112.78

git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard $COMMIT
cmake -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make VERBOSE=1
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
