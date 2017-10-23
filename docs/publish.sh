#!/bin/bash

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=../envoy-docs
PUBLISH_DIR="$CHECKOUT_DIR"/envoy
BUILD_SHA=`git rev-parse HEAD`

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  echo 'cloning'
  git clone git@github.com:envoyproxy/envoyproxy.github.io "$CHECKOUT_DIR"

  git -C "$CHECKOUT_DIR" fetch
  git -C "$CHECKOUT_DIR" checkout -B master origin/master
  rm -fr "$PUBLISH_DIR"
  mkdir -p "$PUBLISH_DIR"
  cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
  cd "$CHECKOUT_DIR"

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
