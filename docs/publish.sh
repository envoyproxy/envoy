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

BUILD_SHA=$(git rev-parse HEAD)

VERSION="$(cat VERSION.txt)"
MAIN_BRANCH="refs/heads/main"
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
fi
