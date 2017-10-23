#!/bin/bash

set -e

DOCS_DIR=generated/docs
PUBLISH_DIR=../envoy-docs
BUILD_SHA=`git rev-parse HEAD`

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  echo 'cloning'
  git clone git@github.com:envoyproxy/envoyproxy.github.io $PUBLISH_DIR

  git -C $PUBLISH_DIR fetch
  git -C $PUBLISH_DIR checkout -B master origin/master
  rm -fr $PUBLISH_DIR/envoy/*
  cp -r $DOCS_DIR/envoy/* $PUBLISH_DIR
  cd $PUBLISH_DIR

  git config user.name "envoy-docs(travis)"
  git config user.email envoy-docs@users.noreply.github.com
  echo 'add'
  git add .
  echo 'commit'
  git commit -m "docs @$BUILD_SHA"
  echo 'push'
  git push origin master
else
  echo "Ignoring PR branch for docs push"
fi
