#!/bin/bash

set -e

DOCS_DIR=generated/docs
PUBLISH_DIR=../envoy-docs
BUILD_SHA=`git rev-parse HEAD`

if [ 0==0 ]
then
  echo "Setting up ssh"
  mkdir -p ~/.ssh
  echo -e "Host github.com\n\tHostName github.com\n\tUser git\n\tIdentityFile .publishdocskey\n\tStrictHostKeyChecking no\n" >> ~/.ssh/config

  echo "Setting up Git Access"
  chmod 600 .publishdocskey

  # Add the SSH key so it's used on git commands
  eval `ssh-agent -s`
  ssh-add .publishdocskey

  echo 'cloning'
  git clone https://github.com/lyft/envoy $PUBLISH_DIR

  git -C $PUBLISH_DIR fetch
  git -C $PUBLISH_DIR checkout -B gh-pages-test origin/gh-pages
  rm -fr $PUBLISH_DIR/*
  cp -r $DOCS_DIR/* $PUBLISH_DIR
  cd $PUBLISH_DIR

  git config user.name "lyft-buildnotify(travis)"
  echo 'email'
  git config user.email travis@travis.com
  echo 'add'
  git add .
  echo 'commit'
  git commit -m "docs @$BUILD_SHA"
  echo 'set remote to ssh'
   git remote add docs ssh://git@github.com:lyft/envoy
  echo 'push'
  git push docs gh-pages-test
else
  echo "Ignoring branch for docs push"
fi
