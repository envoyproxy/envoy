#!/bin/bash

set -e

# Using a pinned commit rather than release version to avoid taking the Boost
# dependency present in last release.
COMMIT=e2818c423e5058a02f46ce2e519a82742a8ccac9  # 2017-07-25

git clone https://github.com/jbeder/yaml-cpp
cd yaml-cpp
git reset --hard "$COMMIT"
cmake -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
  -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo .
make VERBOSE=1 install
