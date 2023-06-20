#!/bin/bash

# This pushes a prebuilt docs version to the website for published releases.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=envoy-docs
BUILD_SHA=$(git rev-parse HEAD)

VERSION="$(cat VERSION.txt)"
PUBLISH_DIR="${CHECKOUT_DIR}/docs/envoy/v${VERSION}"
DOCS_MAIN_BRANCH="main"

echo 'cloning'
git clone git@github.com:envoyproxy/envoy-website "${CHECKOUT_DIR}" -b "${DOCS_MAIN_BRANCH}" --depth 1

if [[ -e "$PUBLISH_DIR" ]]; then
    # Defense against the unexpected.
    echo 'Docs version already exists, not continuing!.'
    exit 0
    # exit 1
fi

mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "${CHECKOUT_DIR}"

git config user.name "envoy-docs(Azure Pipelines)"
git config user.email envoy-docs@users.noreply.github.com

git add .
git commit -m "docs envoy@$BUILD_SHA"
git push origin "${DOCS_MAIN_BRANCH}"
