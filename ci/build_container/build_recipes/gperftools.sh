#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

# TODO(cmluciano): Bump to release 2.8
# This sha is specifically chosen to fix ppc64le builds that require inclusion
# of asm/ptrace.h
VERSION=fc00474ddc21fff618fc3f009b46590e241e425e
SHA256=18574813a062eee487bc1b761e8024a346075a7cb93da19607af362dc09565ef

curl https://github.com/gperftools/gperftools/archive/${VERSION}.tar.gz -sLo gperftools-"$VERSION".tar.gz \
  && echo "$SHA256" gperftools-"$VERSION".tar.gz | sha256sum --check

tar xf gperftools-"$VERSION".tar.gz
cd gperftools-"${VERSION}"

./autogen.sh

export LDFLAGS="${LDFLAGS} -lpthread"
./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind

# Don't build tests, since malloc_extension_c_test hardcodes -lstdc++, which breaks build when linking against libc++.
make V=1 install-libLTLIBRARIES install-perftoolsincludeHEADERS
