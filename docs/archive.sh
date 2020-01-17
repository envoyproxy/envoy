#!/usr/bin/env bash

. tools/shell_utils.sh

set -e

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.
if [ -n "$CIRCLE_TAG" ]
then
  # Check the git tag matches the version number in the VERSION file.
  VERSION_NUMBER=$(cat VERSION)
  if [ "v${VERSION_NUMBER}" != "${CIRCLE_TAG}" ]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${CIRCLE_TAG} vs $(cat VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/intro/version_history.rst \
    || (echo "Git tag not found in version_history.rst" && exit 1)

  # Now that we now there is a match, we can use the tag.
  export ENVOY_DOCS_VERSION_STRING="tag-$CIRCLE_TAG"
else
  BUILD_SHA=$(git rev-parse HEAD)
  VERSION_NUM=$(cat VERSION)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUM}"-"${BUILD_SHA:0:6}"
fi

[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs

echo "building archive..."
time tar -cJf /tmp/docs-${ENVOY_DOCS_VERSION_STRING}.tar.xz ${DOCS_OUTPUT_DIR}
mv /tmp/docs-${ENVOY_DOCS_VERSION_STRING}.tar.xz ${DOCS_OUTPUT_DIR}
echo "...archive docs-${ENVOY_DOCS_VERSION_STRING}.tar.xz built!"

echo "cleaning up doxygen"
rm -rf ${DOCS_OUTPUT_DIR}/doxygen

echo "building doxygen replacement page"
mkdir -p ${DOCS_OUTPUT_DIR}/doxygen/html
echo "<p>Doxygen archived due to size. <a href='../../docs-${ENVOY_DOCS_VERSION_STRING}.tar.xz'>Download the archive to view</a>" > ${DOCS_OUTPUT_DIR}/doxygen/html/index.html