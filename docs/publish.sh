#!/bin/bash

set -e

DOCS_DIR=$1
PUBLISH_DIR=$2
BUILD_SHA=`git rev-parse HEAD`

if [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_BRANCH" == "master" ]
then
  git clone https://GH_TOKEN@github.com/lyft/envoy $PUBLISH_DIR
  git -C $PUBLISH_DIR config user.name "Publish Docs"
  git -C $PUBLISH_DIR config user.email GH_EMAIL
  git -C $PUBLISH_DIR fetch
  git -C $PUBLISH_DIR checkout -B gh-pages origin/gh-pages
  rm -fr $PUBLISH_DIR/*
  cp -r $DOCS_DIR/* $PUBLISH_DIR
  git -C $PUBLISH_DIR add .
  git -C $PUBLISH_DIR commit -m "docs @$BUILD_SHA"
  git -C $PUBLISH_DIR push origin gh-pages
fi
