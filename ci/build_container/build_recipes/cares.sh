#!/bin/bash

set -e

TAG=cares-1_15_0
VERSION=c-ares-1.15.0
SHA256=6cdb97871f2930530c97deb7cf5c8fa4be5a0b02c7cea6e7c7667672a39d6852

# cares is fussy over whether -D appears inside CFLAGS vs. CPPFLAGS, oss-fuzz
# sets CFLAGS with -D, so we need to impedance match here. In turn, OS X automake
# is fussy about newlines in CFLAGS/CPPFLAGS, so translate them into spaces.
CPPFLAGS="$(for f in $CXXFLAGS; do if [[ $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"
CFLAGS="$(for f in $CXXFLAGS; do if [[ ! $f =~ -D.* ]]; then echo $f; fi; done | tr '\n' ' ')"

curl https://github.com/c-ares/c-ares/releases/download/"$TAG"/"$VERSION".tar.gz -sLo "$VERSION".tar.gz \
  && echo "$SHA256" "$VERSION".tar.gz | sha256sum --check
tar xf "$VERSION".tar.gz
cd "$VERSION"

mkdir build
cd build

build_type=RelWithDebInfo
if [[ "${OS}" == "Windows_NT" ]]; then
  build_type=Release
  if [[ "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
    # On Windows, every object file in the final executable needs to be compiled to use the
    # same version of the C Runtime Library -- there are different versions for debug and
    # release builds. The script "ci/do_ci.ps1" will pass BAZEL_WINDOWS_BUILD_TYPE=dbg
    # to bazel when performing a debug build.
    build_type=Debug
  fi
fi

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DCARES_SHARED=no \
  -DCARES_STATIC=on \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # .pdb files are not generated for release builds
  cp "CMakeFiles/c-ares.dir/c-ares.pdb" "$THIRDPARTY_BUILD/lib/c-ares.pdb"
fi
