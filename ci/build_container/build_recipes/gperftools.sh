#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

VERSION=fc00474ddc21fff618fc3f009b46590e241e425e
SHA256=a2b6da3562addd0ffba2cc7731867033cd0f548888053a22dc358226ad005912

curl https://api.github.com/repos/gperftools/gperftools/tarball/"$VERSION" -sLo gperftools-"$VERSION".tar.gz \
  && echo "$SHA256" gperftools-"$VERSION".tar.gz | sha256sum --check

tar xf gperftools-"$VERSION".tar.gz
cd gperftools-gperftools-"${VERSION:0:7}"

./autogen.sh

export LDFLAGS="${LDFLAGS} -lpthread"
./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind

# Don't build tests, since malloc_extension_c_test hardcodes -lstdc++, which breaks build when linking against libc++.
make V=1 install-libLTLIBRARIES install-perftoolsincludeHEADERS
