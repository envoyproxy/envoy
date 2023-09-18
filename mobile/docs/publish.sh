#!/bin/bash -e

# This is run on every commit that GitHub Actions picks up. It assumes that docs have already been
# built via docs/build.sh. The push behavior differs depending on the nature of the commit:
# * Tag commit (e.g. v1.6.0): pushes docs to versioned location.
# * Main commit: pushes docs to latest. Note that envoy-mobile.github.io uses `master` rather than
#                `main` because using `main` as the default branch currently results in 404s.
# * Otherwise: noop.

set -o pipefail

DOCS_DIR=generated/docs
BUILD_SHA="$(git rev-parse HEAD)"

if [[ -z "$MOBILE_DOCS_CHECKOUT_DIR" ]]; then
    echo "MOBILE_DOCS_CHECKOUT_DIR is not set, exiting" >&2
    exit 1
fi

if [[ "$GITHUB_REF_TYPE" == "tag" ]]; then
    PUBLISH_DIR="$MOBILE_DOCS_CHECKOUT_DIR"/docs/envoy-mobile/"$GITHUB_REF_NAME"
else
    PUBLISH_DIR="$MOBILE_DOCS_CHECKOUT_DIR"/docs/envoy-mobile/latest
fi

echo "Publishing docs in ${PUBLISH_DIR}"

git -C "$MOBILE_DOCS_CHECKOUT_DIR" checkout -B master origin/master
rm -fr "$PUBLISH_DIR"
mkdir -p "$PUBLISH_DIR"
cp -r "$DOCS_DIR"/* "$PUBLISH_DIR"
cd "$MOBILE_DOCS_CHECKOUT_DIR" || exit 1

git config user.name "envoy-mobile-docs(ci)"
git config user.email envoy-mobile-docs@users.noreply.github.com
echo 'add'
git add .
echo 'commit'
git commit -m "docs envoy-mobile@$BUILD_SHA"

if [[ "$MOBILE_PUSH_CHANGES" == "true" ]]; then
    echo 'push'
    git push origin master
else
    git diff
    echo "Not pushing for pull request"
fi
