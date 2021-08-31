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

RELEASE_TAG_REGEX="^refs/tags/v.*"

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    DOCS_TAG="${AZP_BRANCH/refs\/tags\//}"
    export DOCS_TAG
else
    BUILD_SHA=$(git rev-parse HEAD)
    export BUILD_SHA
fi

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=(
    "--action_env=DOCS_TAG"
    "--action_env=BUILD_SHA"
    "--action_env=SPHINX_SKIP_CONFIG_VALIDATION")

bazel build "${BAZEL_BUILD_OPTIONS[@]}" //docs:html

[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"
tar -xf bazel-bin/docs/html.tar -C "$DOCS_OUTPUT_DIR"
