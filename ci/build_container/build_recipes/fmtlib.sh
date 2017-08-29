#!/bin/bash

set -e

VERSION=4.0.0

wget -O fmt-"$VERSION".tar.gz https://github.com/fmtlib/fmt/archive/"$VERSION".tar.gz
tar xf fmt-"$VERSION".tar.gz
rsync -av fmt-"$VERSION"/* "$THIRDPARTY_SRC"/fmtlib
