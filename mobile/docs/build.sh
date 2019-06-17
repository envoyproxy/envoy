#!/bin/bash

set -e

. envoy/tools/shell_utils.sh

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
  export ENVOY_DOCS_RELEASE_LEVEL=tagged
  export ENVOY_BLOB_SHA="$CIRCLE_TAG"
else
  BUILD_SHA=$(git rev-parse HEAD)
  VERSION_NUM=$(cat VERSION)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUM}"-"${BUILD_SHA:0:6}"
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
pip install -r "${SCRIPT_DIR}"/requirements.txt

rsync -av "${SCRIPT_DIR}"/root/ "${SCRIPT_DIR}"/conf.py "${GENERATED_RST_DIR}"
sphinx-build -W --keep-going -b html "${GENERATED_RST_DIR}" "${DOCS_OUTPUT_DIR}"
