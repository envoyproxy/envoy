#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"

$($SCRIPT_DIR/versions.py gperftools)

FILE_NAME=$(basename "$FILE_URL")

curl "$FILE_URL" -sLo "$FILE_NAME" \
  && echo "$FILE_SHA256" "$FILE_NAME" | sha256sum --check
tar xf "$FILE_NAME"

cd "$FILE_PREFIX"

gunzip -c ../data/gperftools.patch.gz | patch -p1
chmod u+x configure

export LDFLAGS="${LDFLAGS} -lpthread"
./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind

# Don't build tests, since malloc_extension_c_test hardcodes -lstdc++, which breaks build when linking against libc++.
make V=1 install-libLTLIBRARIES install-perftoolsincludeHEADERS
