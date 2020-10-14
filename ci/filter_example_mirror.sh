#!/bin/bash

set -e

ENVOY_SRCDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../" && pwd)
CHECKOUT_DIR=../envoy-filter-example
MAIN_BRANCH="refs/heads/master"
FILTER_EXAMPLE_MAIN_BRANCH="master"

if [[ "${AZP_BRANCH}" == "${MAIN_BRANCH}" ]]; then
  echo "Cloning..."
  git clone git@github.com:envoyproxy/envoy-filter-example "$CHECKOUT_DIR" -b "${FILTER_EXAMPLE_MAIN_BRANCH}"

  git -C "$CHECKOUT_DIR" config user.name "envoy-filter-example(Azure Pipelines)"
  git -C "$CHECKOUT_DIR" config user.email envoy-filter-example@users.noreply.github.com

  echo "Updating Submodule..."
  # Update submodule to latest Envoy SHA
  ENVOY_SHA=$(git rev-parse HEAD)
  git -C "$CHECKOUT_DIR" submodule update --init
  git -C "$CHECKOUT_DIR/envoy" checkout "$ENVOY_SHA"

  echo "Updating Workspace file."
  sed -e "s|{ENVOY_SRCDIR}|envoy|" "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example > "${CHECKOUT_DIR}"/WORKSPACE

  echo "Committing, and Pushing..."
  git -C "$CHECKOUT_DIR" commit -a -m "Update Envoy submodule to $ENVOY_SHA"
  git -C "$CHECKOUT_DIR" push origin "${FILTER_EXAMPLE_MAIN_BRANCH}"
  echo "Done"
fi
