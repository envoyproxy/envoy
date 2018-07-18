#!/bin/bash

set -e

VERSION=0.6.2

curl https://github.com/jbeder/yaml-cpp/archive/yaml-cpp-"$VERSION".tar.gz -sLo yaml-cpp-"$VERSION".tar.gz
tar xf yaml-cpp-"$VERSION".tar.gz
cd yaml-cpp-yaml-cpp-"$VERSION"

mkdir build
cd build

build_type=RelWithDebInfo
if [[ "${OS}" == "Windows_NT" ]]; then
  build_type=Debug
fi

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" \
  -DCMAKE_CXX_FLAGS:STRING="${CXXFLAGS} ${CPPFLAGS}" \
  -DCMAKE_C_FLAGS:STRING="${CFLAGS} ${CPPFLAGS}" \
  -DYAML_CPP_BUILD_TESTS=off \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/yaml-cpp.dir/yaml-cpp.pdb" "$THIRDPARTY_BUILD/lib/yaml-cpp.pdb"
fi
