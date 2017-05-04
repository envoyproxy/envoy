#!/bin/bash

set -e

VERSION=0.11.0

wget -O spdlog-$VERSION.tar.gz https://github.com/gabime/spdlog/archive/v$VERSION.tar.gz
tar xf spdlog-$VERSION.tar.gz
rsync -av spdlog-$VERSION $THIRDPARTY_SRC
rsync -av spdlog-$VERSION/* $THIRDPARTY_SRC/spdlog
