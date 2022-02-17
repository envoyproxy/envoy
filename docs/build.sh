#!/usr/bin/env bash

# set SPHINX_SKIP_CONFIG_VALIDATION environment variable to true to skip
# validation of configuration examples

set -e

if [[ ! $(command -v bazel) ]]; then
    # shellcheck disable=SC2016
    echo 'ERROR: bazel must be installed and available in "$PATH" to build docs' >&2
    exit 1
fi
if [[ ! $(command -v jq) ]]; then
    # shellcheck disable=SC2016
    echo 'ERROR: jq must be installed and available in "$PATH" to build docs' >&2
    exit 1
fi

MAIN_BRANCH="refs/heads/main"
RELEASE_TAG_REGEX="^refs/tags/v.*"

# default is to build html only
BUILD_TYPE=html

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    DOCS_TAG="${AZP_BRANCH/refs\/tags\//}"
    export DOCS_TAG
else
    BUILD_SHA=$(git rev-parse HEAD)
    export BUILD_SHA
    if [[ "${AZP_BRANCH}" == "${MAIN_BRANCH}" ]]; then
        # no need to build html, just rst
        BUILD_TYPE=rst
    fi
fi

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=(
    "--action_env=DOCS_TAG"
    "--action_env=BUILD_SHA"
    "--action_env=SPHINX_SKIP_CONFIG_VALIDATION")

# Building html/rst is determined by then needs of CI but can be overridden in dev.
if [[ "${BUILD_TYPE}" == "html" ]] || [[ -n "${DOCS_BUILD_HTML}" ]]; then
    BUILD_HTML=1
fi
if [[ "${BUILD_TYPE}" == "rst" ]] || [[ -n "${DOCS_BUILD_RST}" ]]; then
    BUILD_RST=1
fi

# Build html/rst
if [[ -n "${BUILD_RST}" ]]; then
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" //docs:rst
fi
if [[ -n "${BUILD_HTML}" ]]; then
    bazel build "${BAZEL_BUILD_OPTIONS[@]}" //docs:html
fi

[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

# Save html/rst to output directory
if [[ -n "${BUILD_HTML}" ]]; then
    tar -xf bazel-bin/docs/html.tar -C "$DOCS_OUTPUT_DIR"
fi
if [[ -n "${BUILD_RST}" ]]; then
    gzip -c bazel-bin/docs/rst.tar > "$DOCS_OUTPUT_DIR"/envoy-docs-rst.tar.gz
fi
