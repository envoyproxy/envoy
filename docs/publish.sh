#!/bin/bash

# This is run on every commit that Azure Pipelines picks up. It assumes that docs have already been built
# via docs/build.sh. The push behavior differs depending on the nature of the commit and non/dev `VERSION.txt`:
# * Main commit (dev):
#     pushes docs to https://www.envoyproxy.io/docs/envoy/latest/.
# * Main or release branch commit (non-dev):
#     pushes docs to versioned location, e.g.
#       https://www.envoyproxy.io/docs/envoy/v1.6.0/.
# * Otherwise: noop.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=envoy-docs
BUILD_SHA=$(git rev-parse HEAD)

VERSION="$(cat VERSION.txt)"
MAIN_BRANCH="refs/heads/main"
RELEASE_BRANCH_REGEX="^refs/heads/release/v.*"
DEV_VERSION_REGEX="-dev$"

if [[ "$VERSION" =~ $DEV_VERSION_REGEX ]]; then
    if [[ "$AZP_BRANCH" == "${MAIN_BRANCH}" ]]; then
        if [[ -n "$NETLIFY_TRIGGER_URL" ]]; then
            echo "Triggering netlify docs build for (${BUILD_SHA})"
            curl -X POST -d "$BUILD_SHA" "$NETLIFY_TRIGGER_URL"
        fi
    else
        echo "Ignoring docs push"
    fi
    exit 0
else
    PUBLISH_DIR="${CHECKOUT_DIR}/docs/envoy/v${VERSION}"
fi

if [[ "${AZP_BRANCH}" != "${MAIN_BRANCH}" ]] && ! [[ "${AZP_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]]; then
    # Most likely a tag, do nothing.
    echo 'Ignoring non-release branch for docs push.'
    exit 0
fi

DOCS_MAIN_BRANCH="main"

echo 'cloning'
git clone git@github.com:envoyproxy/envoy-website "${CHECKOUT_DIR}" -b "${DOCS_MAIN_BRANCH}" --depth 1

if [[ -e "$PUBLISH_DIR" ]]; then
    # Defense against the unexpected.
    echo 'Docs version already exists, not continuing!.'
    exit 1
fi

mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "${CHECKOUT_DIR}"

git config user.name "envoy-docs(Azure Pipelines)"
git config user.email envoy-docs@users.noreply.github.com

set -x

git add .
git commit -m "docs envoy@$BUILD_SHA"
git push origin "${DOCS_MAIN_BRANCH}"
