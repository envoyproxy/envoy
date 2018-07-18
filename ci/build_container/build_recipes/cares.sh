#!/bin/bash

set -e

VERSION=cares-1_14_0

# cares is fussy over whether -D appears inside CFLAGS vs. CPPFLAGS, oss-fuzz
# sets CFLAGS with -D, so we need to impedance match here. In turn, OS X automake
# is fussy about newlines in CFLAGS/CPPFLAGS, so translate them into spaces.
CPPFLAGS="$(for f in $CXXFLAGS; do if [[ $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"
CFLAGS="$(for f in $CXXFLAGS; do if [[ ! $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"

curl https://github.com/c-ares/c-ares/archive/"$VERSION".tar.gz -sLo c-ares-"$VERSION".tar.gz
tar xf c-ares-"$VERSION".tar.gz
cd c-ares-"$VERSION"

mkdir build
cd build

build_type=RelWithDebInfo
if [[ "${OS}" == "Windows_NT" ]]; then
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
