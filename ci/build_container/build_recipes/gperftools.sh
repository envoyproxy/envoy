#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

curl "$GPERFTOOLS_FILE_URL" -sLo gperftools-"$GPERFTOOLS_VERSION".tar.gz \
  && echo "$GPERFTOOLS_FILE_SHA256" gperftools-"$GPERFTOOLS_VERSION".tar.gz | sha256sum --check
tar xf gperftools-"$GPERFTOOLS_VERSION".tar.gz
cd gperftools-"$GPERFTOOLS_VERSION"

LDFLAGS="-lpthread" ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-frame-pointers --disable-libunwind
make V=1 install
