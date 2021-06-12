#!/usr/bin/env bash

# set SPHINX_SKIP_CONFIG_VALIDATION environment variable to true to skip
# validation of configuration examples

. tools/shell_utils.sh

set -e

RELEASE_TAG_REGEX="^refs/tags/v.*"

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  DOCS_TAG="${AZP_BRANCH/refs\/tags\//}"
fi

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.
VERSION_NUMBER=$(cat VERSION)
export DOCKER_IMAGE_TAG_NAME
DOCKER_IMAGE_TAG_NAME=$(echo "$VERSION_NUMBER" | sed -E 's/([0-9]+\.[0-9]+)\.[0-9]+.*/v\1-latest/')
if [[ -n "${DOCS_TAG}" ]]; then
  # Check the git tag matches the version number in the VERSION file.
  if [[ "v${VERSION_NUMBER}" != "${DOCS_TAG}" ]]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${DOCS_TAG} vs $(cat VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/version_history/current.rst \
    || (echo "Git tag not found in version_history/current.rst" && exit 1)

  # Now that we know there is a match, we can use the tag.
  export ENVOY_DOCS_VERSION_STRING="tag-${DOCS_TAG}"
  export ENVOY_DOCS_RELEASE_LEVEL=tagged
  export ENVOY_BLOB_SHA="${DOCS_TAG}"
else
  BUILD_SHA=$(git rev-parse HEAD)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUMBER}"-"${BUILD_SHA:0:6}"
  export ENVOY_DOCS_RELEASE_LEVEL=pre-release
  export ENVOY_BLOB_SHA="$BUILD_SHA"
fi

SCRIPT_DIR="$(dirname "$0")"
BUILD_DIR=build_docs
[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
[[ -z "${GENERATED_RST_DIR}" ]] && GENERATED_RST_DIR=generated/rst

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

rm -rf "${GENERATED_RST_DIR}"
mkdir -p "${GENERATED_RST_DIR}"

source_venv "$BUILD_DIR"
pip3 install --require-hashes -r "${SCRIPT_DIR}"/requirements.txt

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api_canonical

GENERATED_RST_DIR="$(realpath "${GENERATED_RST_DIR}")"
export GENERATED_RST_DIR

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=(
    "--remote_download_outputs=all"
    "--strategy=protodoc=sandboxed,local"
    "--action_env=ENVOY_BLOB_SHA")

bazel build "${BAZEL_BUILD_OPTIONS[@]}" //docs:rst

# TODO(phlax): once all of above jobs are moved to bazel build genrules these can be done as part of the sphinx build
tar -xf bazel-bin/docs/rst.tar -C "${GENERATED_RST_DIR}"

# To speed up validate_fragment invocations in validating_code_block
bazel build "${BAZEL_BUILD_OPTIONS[@]}" //tools/config_validation:validate_fragment

sphinx-build -W --keep-going -b html "${GENERATED_RST_DIR}" "${DOCS_OUTPUT_DIR}"
