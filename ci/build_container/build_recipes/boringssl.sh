#!/bin/bash

set -e

COMMIT=14308731e5446a73ac2258688a9688b524483cb6  # chromium-61.0.3163.81

git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard "$COMMIT"
cmake -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make VERBOSE=1
cp -r include/* "$THIRDPARTY_BUILD"/include
cp ssl/libssl.a "$THIRDPARTY_BUILD"/lib
cp crypto/libcrypto.a "$THIRDPARTY_BUILD"/lib
