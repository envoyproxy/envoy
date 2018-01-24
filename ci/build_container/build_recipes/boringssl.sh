#!/bin/bash

set -e

COMMIT=664e99a6486c293728097c661332f92bf2d847c6  # chromium-63.0.3239.84

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
