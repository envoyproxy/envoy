#!/bin/bash

set -e

# shellcheck disable=SC1091
. tools/shell_utils.sh

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.

# Docs for release tags are reserved for vX.Y.Z versions.
# vX.Y.Z.ddmmyy do not publish tagged docs.
VERSION_NUMBER=$(cat mobile/VERSION)
if [[ "$GITHUB_REF_TYPE" == "tag" ]] && [[ "${VERSION_NUMBER}" =~ ^[0-9]+\.[0-9]+\.[0-9]$ ]]
then
  # Check the git tag matches the version number in the VERSION file.
  if [ "v${VERSION_NUMBER}" != "${GITHUB_REF_NAME}" ]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${GITHUB_REF_NAME} vs $(cat mobile/VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/intro/version_history.rst \
    || (echo "Git tag not found in version_history.rst" && exit 1)

  # Now that we now there is a match, we can use the tag.
  export ENVOY_DOCS_VERSION_STRING="tag-$GITHUB_REF_NAME"
  export ENVOY_DOCS_RELEASE_LEVEL=tagged
  export ENVOY_BLOB_SHA="$GITHUB_REF_NAME"
else
  BUILD_SHA=$(git rev-parse HEAD)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUMBER}"-"${BUILD_SHA:0:6}"
  export ENVOY_DOCS_RELEASE_LEVEL=pre-release
  export ENVOY_BLOB_SHA="$BUILD_SHA"
fi

SCRIPT_DIR=$(dirname "$0")
BUILD_DIR=build_docs
[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
[[ -z "${GENERATED_RST_DIR}" ]] && GENERATED_RST_DIR=generated/rst

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

rm -rf "${GENERATED_RST_DIR}"
mkdir -p "${GENERATED_RST_DIR}"

source_venv "$BUILD_DIR"
pip install -r "${SCRIPT_DIR}"/requirements.txt --no-deps --require-hashes

rsync -av "${SCRIPT_DIR}"/root/ "${SCRIPT_DIR}"/conf.py "${GENERATED_RST_DIR}"
sphinx-build -W --keep-going -b html "${GENERATED_RST_DIR}" "${DOCS_OUTPUT_DIR}"
