#!/bin/bash

# This is run on every commit that Azure Pipelines picks up. It assumes that docs have already been built
# via docs/publish.sh.
# * Tag commit (e.g. v1.6.0): pushes packaged version, e.g.
#   github.com:envoyproxy/envoy-dist versions/1.6.0/build.x64.tar.gz

set -e

DIST_DIR=generated/dist
CHECKOUT_DIR=envoy-dist
BUILD_SHA=$(git rev-parse HEAD)

MAIN_BRANCH="refs/heads/main"
RELEASE_TAG_REGEX="^refs/tags/v.*"

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    RELEASE_TAG="${AZP_BRANCH/refs\/tags\/v/}"
else
    echo "Ignoring packaging push"
    exit 0
fi

DIST_MAIN_BRANCH="main"

echo 'cloning'
git clone git@github.com:envoyproxy/envoy-dist "${CHECKOUT_DIR}" -b "${DIST_MAIN_BRANCH}" --depth 1

PUBLISH_DIR="${CHECKOUT_DIR}/versions/${RELEASE_TAG}"

rm -fr "$PUBLISH_DIR"
mkdir -p "$PUBLISH_DIR"
cp -r "$DIST_DIR"/* "$PUBLISH_DIR"
cd "${CHECKOUT_DIR}"

git config user.name "envoy-dist (Azure Pipelines)"
git config user.email envoy-dist@users.noreply.github.com

git add versions
git commit -m "dist: v${RELEASE_TAG} envoy@${BUILD_SHA}"
git push origin "${DIST_MAIN_BRANCH}"
