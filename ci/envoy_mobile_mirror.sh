#!/bin/bash

set -e

CHECKOUT_DIR=envoy-mobile
ENVOY_MOBILE_MAIN_BRANCH="main"

echo "Cloning..."
git clone https://github.com/envoyproxy/envoy-mobile.git

git -C "$CHECKOUT_DIR" config user.name "envoy-filter-example(Azure Pipelines)"
git -C "$CHECKOUT_DIR" config user.email envoy-filter-example@users.noreply.github.com

echo "Updating Submodule..."
# Update submodule to latest Envoy SHA
ENVOY_SHA=$(git rev-parse HEAD)
git -C "$CHECKOUT_DIR" submodule update --init
git -C "$CHECKOUT_DIR/envoy" checkout "$ENVOY_SHA"

#echo "Updating Workspace file."
#sed -e "s|{ENVOY_SRCDIR}|envoy|" "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example > "${CHECKOUT_DIR}"/WORKSPACE

echo "Committing, and Pushing..."
git -C "$CHECKOUT_DIR" commit -a -m "Update Envoy submodule to $ENVOY_SHA"
git -C "$CHECKOUT_DIR" push origin "${ENVOY_MOBILE_MAIN_BRANCH}"
echo "Done"
