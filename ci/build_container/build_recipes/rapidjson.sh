#!/bin/bash

set -e

VERSION=1.1.0

wget -O rapidjson-"$VERSION".tar.gz https://github.com/miloyip/rapidjson/archive/v"$VERSION".tar.gz
tar xf rapidjson-"$VERSION".tar.gz
rsync -av rapidjson-"$VERSION"/* "$THIRDPARTY_SRC"/rapidjson
