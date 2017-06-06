#!/bin/bash

set -e

COMMIT=c8ff30cbe716c72279a6f6a9d7d7d0d4091220fa  # chromium-59.0.3071.86

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
