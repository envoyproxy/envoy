#!/bin/bash

set -e

ENVOY_SRCDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../" && pwd)
CHECKOUT_DIR=../envoy-filter-example

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  echo "Cloning..."
  git clone git@github.com:envoyproxy/envoy-filter-example "$CHECKOUT_DIR"

  git -C "$CHECKOUT_DIR" config user.name "envoy-filter-example(CircleCI)"
  git -C "$CHECKOUT_DIR" config user.email envoy-filter-example@users.noreply.github.com
  git -C "$CHECKOUT_DIR" fetch
  git -C "$CHECKOUT_DIR" checkout -B master origin/master

  echo "Updating Submodule..."
  # Update submodule to latest Envoy SHA
  ENVOY_SHA=$(git rev-parse HEAD)
  git -C "$CHECKOUT_DIR" submodule update --init
  git -C "$CHECKOUT_DIR/envoy" checkout "$ENVOY_SHA"

  echo "Updating Workspace file."
  sed -e "s|{ENVOY_SRCDIR}|envoy|" "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example > "${CHECKOUT_DIR}"/WORKSPACE

  echo "Committing, and Pushing..."
  git -C "$CHECKOUT_DIR" commit -a -m "Update Envoy submodule to $ENVOY_SHA"
  git -C "$CHECKOUT_DIR" push origin master
  echo "Done"
fi
