#!/bin/bash

set -e

VERSION=1.2.1

wget -O tclap-$VERSION.tar.gz https://sourceforge.net/projects/tclap/files/tclap-$VERSION.tar.gz/download
tar xf tclap-$VERSION.tar.gz
rsync -av tclap-$VERSION $THIRDPARTY_SRC
