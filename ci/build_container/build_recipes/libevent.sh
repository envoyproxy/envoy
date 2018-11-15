#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

# Maintainer provided source tarball does not contain cmake content so using Github tarball.
curl "$LIBEVENT_FILE_URL" -sLo libevent-"$LIBEVENT_TAG".tar.gz \
  && echo "$LIBEVENT_FILE_SHA256" libevent-"$LIBEVENT_TAG".tar.gz | sha256sum --check
tar xf libevent-"$LIBEVENT_TAG".tar.gz
cd libevent-"$LIBEVENT_TAG"

mkdir build
cd build

# libevent defaults CMAKE_BUILD_TYPE to Release
build_type=Release
if [[ "${OS}" == "Windows_NT" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library. If Envoy is built with '-c dbg', then it will
  # use the Debug C Runtime Library. Setting CMAKE_BUILD_TYPE to Debug will cause libevent
  # to use the debug version as well
  # TODO: when '-c fastbuild' and '-c opt' work for Windows builds, set this appropriately
  build_type=Debug
fi

cmake -G "Ninja" \
  -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DEVENT__DISABLE_OPENSSL:BOOL=on \
  -DEVENT__DISABLE_REGRESS:BOOL=on \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/event.dir/event.pdb" "$THIRDPARTY_BUILD/lib/event.pdb"
fi
