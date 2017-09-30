#!/bin/bash

set -e

VERSION=2.0.5

wget -O LuaJIT-"$VERSION".tar.gz https://github.com/LuaJIT/LuaJIT/archive/v"$VERSION".tar.gz
tar xf LuaJIT-"$VERSION".tar.gz
cd LuaJIT-"$VERSION"
make V=1 PREFIX="$THIRDPARTY_BUILD" install
