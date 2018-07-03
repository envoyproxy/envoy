#!/bin/bash

set -e

VERSION=cares-1_14_0

# cares is fussy over whether -D appears inside CFLAGS vs. CPPFLAGS, oss-fuzz
# sets CFLAGS with -D, so we need to impedance match here. In turn, OS X automake
# is fussy about newlines in CFLAGS/CPPFLAGS, so translate them into spaces.
CPPFLAGS="$(for f in $CXXFLAGS; do if [[ $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"
CFLAGS="$(for f in $CXXFLAGS; do if [[ ! $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"

wget -O c-ares-"$VERSION".tar.gz https://github.com/c-ares/c-ares/archive/"$VERSION".tar.gz
tar xf c-ares-"$VERSION".tar.gz
cd c-ares-"$VERSION"

if [[ "${OS}" == "Windows_NT" ]]; then
  mkdir build
  cd build
  cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
    -DCARES_SHARED=no \
    -DCARES_STATIC=on \
    -DCMAKE_BUILD_TYPE=Debug \
    ..
  ninja
  ninja install
  cp "CMakeFiles/c-ares.dir/c-ares.pdb" "$THIRDPARTY_BUILD/lib/c-ares.pdb"
else
  ./buildconf
  ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-lib-only \
    --enable-debug --enable-optimize
  make V=1 install
fi
