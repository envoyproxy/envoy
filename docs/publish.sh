#!/bin/bash

set -e

DOCS_DIR=$1
PUBLISH_DIR=$2
BUILD_SHA=`git rev-parse HEAD`

if [[ ! -d $PUBLISH_DIR ]]; then
    echo "$PUBLISH_DIR does not exist. Clone a fresh envoy repo."
fi

git -C $PUBLISH_DIR fetch
git -C $PUBLISH_DIR checkout -B gh-pages origin/gh-pages
rm -fr $PUBLISH_DIR/*
cp -r $DOCS_DIR/* $PUBLISH_DIR
git -C $PUBLISH_DIR add .
git -C $PUBLISH_DIR commit -m "docs @$BUILD_SHA"

echo
echo "*** YOU MUST MANUALLY PUSH the gh-pages branch after verifying the commit ***"
