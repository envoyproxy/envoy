#!/bin/bash

set -e

VERSION=2.0.5

export CPPFLAGS="$CPPFLAGS -DLUAJIT_ENABLE_LUA52COMPAT"

wget -O LuaJIT-"$VERSION".tar.gz https://github.com/LuaJIT/LuaJIT/archive/v"$VERSION".tar.gz
tar xf LuaJIT-"$VERSION".tar.gz
cd LuaJIT-"$VERSION"
make V=1 PREFIX="$THIRDPARTY_BUILD" install
