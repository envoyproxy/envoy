#!/bin/bash

set -e

VERSION=0.6.2

curl https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-"$VERSION".tar.gz -sLo yaml-cpp-"$VERSION".tar.gz
tar xf yaml-cpp-"$VERSION".tar.gz
cd yaml-cpp-yaml-cpp-"$VERSION"

mkdir build
cd build

if [[ "${OS}" == "Windows_NT" ]]; then
  cmake -G"Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
    -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
    -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
    -DYAML_CPP_BUILD_TESTS=off \
    -DCMAKE_BUILD_TYPE=Debug ..
  ninja
  ninja install
  cp "CMakeFiles/yaml-cpp.dir/yaml-cpp.pdb" "$THIRDPARTY_BUILD/lib/yaml-cpp.pdb"
else
  cmake -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
    -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
    -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
    -DCMAKE_BUILD_TYPE=RelWithDebugInfo ..
  make VERBOSE=1 install
fi
