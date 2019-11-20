#!/bin/bash

# This is run on every commit that CircleCI picks up. It assumes that docs have already been built
# via docs/build.sh. The push behavior differs depending on the nature of the commit:
# * Tag commit (e.g. v1.6.0): pushes docs to versioned location, e.g.
#   https://www.envoyproxy.io/docs/envoy/v1.6.0/.
# * Master commit: pushes docs to https://www.envoyproxy.io/docs/envoy/latest/.
# * Otherwise: noop.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=../envoy-docs
BUILD_SHA=`git rev-parse HEAD`

if [ -n "$CIRCLE_TAG" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy/"$CIRCLE_TAG"
elif [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy/latest
else
  echo "Ignoring docs push"
  exit 0
fi

echo 'cloning'
git clone git@github.com:envoyproxy/envoyproxy.github.io "$CHECKOUT_DIR"

git -C "$CHECKOUT_DIR" fetch
git -C "$CHECKOUT_DIR" checkout -B master origin/master
rm -fr "$PUBLISH_DIR"
mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "$CHECKOUT_DIR"

git config user.name "envoy-docs(travis)"
git config user.email envoy-docs@users.noreply.github.com
echo 'add'
git add .
echo 'commit'
git commit -m "docs envoy@$BUILD_SHA"
echo 'push'
git push origin master
