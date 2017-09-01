#!/bin/bash

set -e

VERSION=1.3

wget -O backward-cpp-"$VERSION".tar.gz https://github.com/bombela/backward-cpp/archive/v"$VERSION".tar.gz
tar xf backward-cpp-"$VERSION".tar.gz
cp backward-cpp-"$VERSION"/backward.hpp "$THIRDPARTY_BUILD"/include
