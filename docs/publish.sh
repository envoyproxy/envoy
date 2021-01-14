#!/bin/bash

# This is run on every commit that Azure Pipelines picks up. It assumes that docs have already been built
# via docs/build.sh. The push behavior differs depending on the nature of the commit:
# * Tag commit (e.g. v1.6.0): pushes docs to versioned location, e.g.
#   https://www.envoyproxy.io/docs/envoy/v1.6.0/.
# * Master commit: pushes docs to https://www.envoyproxy.io/docs/envoy/latest/.
# * Otherwise: noop.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=envoy-docs
BUILD_SHA=$(git rev-parse HEAD)

MAIN_BRANCH="refs/heads/master"
RELEASE_TAG_REGEX="^refs/tags/v.*"

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  PUBLISH_DIR="${CHECKOUT_DIR}"/docs/envoy/"${AZP_BRANCH/refs\/tags\//}"
elif [[ "$AZP_BRANCH" == "${MAIN_BRANCH}" ]]; then
  PUBLISH_DIR="${CHECKOUT_DIR}"/docs/envoy/latest
else
  echo "Ignoring docs push"
  exit 0
fi

DOCS_MAIN_BRANCH="master"

echo 'cloning'
git clone git@github.com:envoyproxy/envoyproxy.github.io "${CHECKOUT_DIR}" -b "${DOCS_MAIN_BRANCH}" --depth 1

rm -fr "$PUBLISH_DIR"
mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "${CHECKOUT_DIR}"

git config user.name "envoy-docs(Azure Pipelines)"
git config user.email envoy-docs@users.noreply.github.com

set -x

git add .
git commit -m "docs envoy@$BUILD_SHA"
git push origin "${DOCS_MAIN_BRANCH}"
