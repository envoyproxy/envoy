#!/bin/bash

# This is run on every commit that CircleCI picks up. It assumes that docs have already been built
# via docs/build.sh. The push behavior differs depending on the nature of the commit:
# * Tag commit (e.g. v1.6.0): pushes docs to versioned location.
# * Main commit: pushes docs to latest.
# * Otherwise: noop.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=../envoy-mobile-docs
BUILD_SHA=`git rev-parse HEAD`

if [ -n "$CIRCLE_TAG" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy-mobile/"$CIRCLE_TAG"
elif [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "main" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy-mobile/latest
else
  echo "Ignoring docs push"
  exit 0
fi

echo 'cloning'
git clone git@github.com:envoy-mobile/envoy-mobile.github.io "$CHECKOUT_DIR"

git -C "$CHECKOUT_DIR" fetch
git -C "$CHECKOUT_DIR" checkout -B main origin/main
rm -fr "$PUBLISH_DIR"
mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "$CHECKOUT_DIR"

git config user.name "envoy-mobile-docs(ci)"
git config user.email envoy-mobile-docs@users.noreply.github.com
echo 'add'
git add .
echo 'commit'
git commit -m "docs envoy-mobile@$BUILD_SHA"
echo 'push'
git push origin main
