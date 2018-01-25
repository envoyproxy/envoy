#!/bin/bash

set -e

COMMIT=a20bb7ff8bb5057065a2e7941249773f9676cf45  # chromium-64.0.3282.119

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
