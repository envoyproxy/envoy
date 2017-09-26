#!/bin/bash

set -e

VERSION=1-2-1-release-final

wget -O tclap-"$VERSION".tar.gz https://github.com/eile/tclap/archive/tclap-"$VERSION".tar.gz
tar xf tclap-"$VERSION".tar.gz
rsync -av tclap-tclap-"$VERSION"/* "$THIRDPARTY_SRC"/tclap
