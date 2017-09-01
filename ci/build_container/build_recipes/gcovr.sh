#!/bin/bash

set -e

VERSION=3.3

wget -O gcovr-"$VERSION".tar.gz https://github.com/gcovr/gcovr/archive/"$VERSION".tar.gz
tar xf gcovr-"$VERSION".tar.gz
rsync -av gcovr-"$VERSION"/* "$THIRDPARTY_SRC"/gcovr
