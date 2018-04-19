#!/bin/bash

set -e

CHECKOUT_DIR=../envoy-filter-example

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  echo "Cloning..."
  git clone git@github.com:envoyproxy/envoy-filter-example "$CHECKOUT_DIR"

  git -C "$CHECKOUT_DIR" config user.name "envoy-filter-example(CircleCI)"
  git -C "$CHECKOUT_DIR" config user.email envoy-filter-example@users.noreply.github.com
  git -C "$CHECKOUT_DIR" fetch
  git -C "$CHECKOUT_DIR" checkout -B master origin/master

  # Update submodule to latest Envoy SHA
  ENVOY_SHA=$(git rev-parse HEAD)
  git -C "$CHECKOUT_DIR" submodule update --init
  git -C "$CHECKOUT_DIR/envoy" checkout "$ENVOY_SHA"
  git -C "$CHECKOUT_DIR" commit -a -m "Update Envoy submodule to $ENVOY_SHA"

  echo "Pushing..."
  git -C "$CHECKOUT_DIR" push origin master
  echo "Done"
fi
