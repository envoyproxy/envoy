#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

# cares is fussy over whether -D appears inside CFLAGS vs. CPPFLAGS, oss-fuzz
# sets CFLAGS with -D, so we need to impedance match here. In turn, OS X automake
# is fussy about newlines in CFLAGS/CPPFLAGS, so translate them into spaces.
CPPFLAGS="$(for f in $CXXFLAGS; do if [[ $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"
CFLAGS="$(for f in $CXXFLAGS; do if [[ ! $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"

curl "$CARES_FILE_URL" -sLo c-ares-"$CARES_VERSION".tar.gz \
  && echo "$CARES_FILE_SHA256" c-ares-"$CARES_VERSION".tar.gz | sha256sum --check
tar xf c-ares-"$CARES_VERSION".tar.gz
cd c-ares-"$CARES_VERSION"

mkdir build
cd build

build_type=RelWithDebInfo
if [[ "${OS}" == "Windows_NT" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library. If Envoy is built with '-c dbg', then it will
  # use the Debug C Runtime Library. Setting CMAKE_BUILD_TYPE to Debug will cause c-ares
  # to use the debug version as well
  # TODO: when '-c fastbuild' and '-c opt' work for Windows builds, set this appropriately
  build_type=Debug
fi

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DCARES_SHARED=no \
  -DCARES_STATIC=on \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/c-ares.dir/c-ares.pdb" "$THIRDPARTY_BUILD/lib/c-ares.pdb"
fi
