#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

curl "$ZLIB_FILE_URL" -sLo zlib-"$ZLIB_VERSION".tar.gz \
  && echo "$ZLIB_FILE_SHA256" zlib-"$ZLIB_VERSION".tar.gz | sha256sum --check
tar xf zlib-"$ZLIB_VERSION".tar.gz
cd zlib-"$ZLIB_VERSION"

mkdir build
cd build

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX:PATH="$THIRDPARTY_BUILD" ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "CMakeFiles/zlibstatic.dir/zlibstatic.pdb" "$THIRDPARTY_BUILD/lib/zlibstatic.pdb"
fi
