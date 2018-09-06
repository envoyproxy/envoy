#!/bin/bash

set -e

# Pin to this commit to pick up fix for building on Visual Studio 15.8
COMMIT=0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79 # 2018-06-30
SHA256=53dcffd55f3433b379fcc694f45c54898711c0e29159a7bd02e82a3e0253bac3

curl https://github.com/jbeder/yaml-cpp/archive/"$COMMIT".tar.gz -sLo yaml-cpp-"$COMMIT".tar.gz \
  && echo "$SHA256" yaml-cpp-"$COMMIT".tar.gz | sha256sum --check
tar xf yaml-cpp-"$COMMIT".tar.gz
cd yaml-cpp-"$COMMIT"

mkdir build
cd build

build_type=RelWithDebInfo
if [[ "${OS}" == "Windows_NT" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library. If Envoy is built with '-c dbg', then it will
  # use the Debug C Runtime Library. Setting CMAKE_BUILD_TYPE to Debug will cause yaml-cpp
  # to use the debug version as well
  # TODO: when '-c fastbuild' and '-c opt' work for Windows builds, set this appropriately
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
