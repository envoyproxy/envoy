#!/bin/bash

set -e

if [ -z "$CIRCLE_TAG" ]
then
  echo "Ignoring non-tag for docs push"
  exit 0
fi

export ENVOY_DOCS_VERSION_STRING="tag-$CIRCLE_TAG"
export ENVOY_DOCS_RELEASE_LEVEL=tagged

# Build the docs by cloning data-plane-api and building at the embedded SHA.
DOCS_BUILD_DIR="${BUILD_DIR}"/docs
rm -rf "${DOCS_BUILD_DIR}" generated/docs generated/rst
mkdir -p "${DOCS_BUILD_DIR}"
ENVOY_API=$(bazel/git_repository_info.py envoy_api)
read -a GIT_INFO <<< "${ENVOY_API}"
pushd "${DOCS_BUILD_DIR}"
git clone "${GIT_INFO[0]}"
cd data-plane-api
git checkout "${GIT_INFO[1]}"
# Check the git tag matches the version number in the VERSION file.
VERSION_NUMBER=$(cat VERSION)
if [ "v${VERSION_NUMBER}" != "${CIRCLE_TAG}" ]; then
  echo "Given git tag does not match the VERSION file content:"
  echo "${CIRCLE_TAG} vs $(cat VERSION)"
  exit 1
fi
# Check the version_history.rst contains current release version.
grep --fixed-strings "$VERSION_NUMBER" docs/root/intro/version_history.rst
./docs/build.sh
popd
rsync -av "${DOCS_BUILD_DIR}"/data-plane-api/generated/* generated/

# Now publish them into a directory specific to the tag.
DOCS_DIR=generated/docs
CHECKOUT_DIR=../envoy-docs
PUBLISH_DIR="$CHECKOUT_DIR"/docs/envoy/"$CIRCLE_TAG"
BUILD_SHA=`git rev-parse HEAD`

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
