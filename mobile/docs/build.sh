#!/usr/bin/env bash

set -e

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.

# Docs for release tags are reserved for vX.Y.Z versions.
# vX.Y.Z.ddmmyy do not publish tagged docs.
VERSION_NUMBER=$(cat VERSION)
if [[ "$GITHUB_REF_TYPE" == "tag" ]] && [[ "${VERSION_NUMBER}" =~ ^[0-9]+\.[0-9]+\.[0-9]$ ]]
then
  # Check the git tag matches the version number in the VERSION file.
  if [ "v${VERSION_NUMBER}" != "${GITHUB_REF_NAME}" ]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${GITHUB_REF_NAME} vs $(cat VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/intro/version_history.rst \
    || (echo "Git tag not found in version_history.rst" && exit 1)
  DOCS_TARGET=//docs
else
  DOCS_TARGET=//docs:html
fi

[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"
DOCS_OUTPUT_DIR="$(realpath "$DOCS_OUTPUT_DIR")"

./bazelw run \
         "--@envoy//tools/tarball:target=$DOCS_TARGET" \
         @envoy//tools/tarball:unpack \
         "$DOCS_OUTPUT_DIR"
