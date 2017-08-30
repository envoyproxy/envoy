#!/bin/bash

set -e

VERSION=2.7.1

wget -O http-parser-"$VERSION".tar.gz https://github.com/nodejs/http-parser/archive/v"$VERSION".tar.gz
tar xf http-parser-"$VERSION".tar.gz
cd http-parser-"$VERSION"
"$CC" $CFLAGS "$CPPFLAGS" -c http_parser.c -o http_parser.o
ar rcs libhttp_parser.a http_parser.o
cp libhttp_parser.a "$THIRDPARTY_BUILD"/lib
cp http_parser.h "$THIRDPARTY_BUILD"/include
