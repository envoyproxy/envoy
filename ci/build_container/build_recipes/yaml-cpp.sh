#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

curl "$YAMLCPP_FILE_URL" -sLo yaml-cpp-"$YAMLCPP_GIT_SHA".tar.gz \
  && echo "$YAMLCPP_FILE_SHA256" yaml-cpp-"$YAMLCPP_GIT_SHA".tar.gz | sha256sum --check
tar xf yaml-cpp-"$YAMLCPP_GIT_SHA".tar.gz
cd yaml-cpp-"$YAMLCPP_GIT_SHA"

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
