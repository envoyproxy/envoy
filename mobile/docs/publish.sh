#!/bin/bash

# This is run on every commit that GitHub Actions picks up. It assumes that docs have already been
# built via docs/build.sh. The push behavior differs depending on the nature of the commit:
# * Tag commit (e.g. v1.6.0): pushes docs to versioned location.
# * Main commit: pushes docs to latest. Note that envoy-mobile.github.io uses `master` rather than
#                `main` because using `main` as the default branch currently results in 404s.
# * Otherwise: noop.

set -e

DOCS_DIR=generated/docs
CHECKOUT_DIR=../envoy-mobile-docs
BUILD_SHA="$(git rev-parse HEAD)"

if [ "$GITHUB_REF_TYPE" == "tag" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy-mobile/"$GITHUB_REF_NAME"
elif [ "$GITHUB_REF_NAME" == "main" ]
then
  PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy-mobile/latest
else
  echo "Ignoring docs push"
  exit 0
fi

echo 'cloning'
git clone git@github.com:envoy-mobile/envoy-mobile.github.io "$CHECKOUT_DIR"

git -C "$CHECKOUT_DIR" fetch
git -C "$CHECKOUT_DIR" checkout -B master origin/master
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
git push origin master
