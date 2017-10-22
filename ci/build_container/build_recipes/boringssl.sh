#!/bin/bash

set -e

# TODO(zuercher): Xcode 9 seems to need this or else boringssl doesn't compile
if [[ `uname` == "Darwin" ]];
then
    export CPPFLAGS="$CPPFLAGS -D_DARWIN_C_SOURCE"
fi

COMMIT=ae9f0616c58bddcbe7a6d80d29d796bee9aaff2e  # chromium-62.0.3202.62

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
