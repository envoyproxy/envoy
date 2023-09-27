#!/usr/bin/env bash

# set SPHINX_SKIP_CONFIG_VALIDATION environment variable to true to skip
# validation of configuration examples

set -e


if [[ ! $(command -v bazel) ]]; then
    # shellcheck disable=SC2016
    echo 'ERROR: bazel must be installed and available in "$PATH" to build docs' >&2
    exit 1
fi

VERSION="$(cat VERSION.txt)"
MAIN_BRANCH="refs/heads/main"
DEV_VERSION_REGEX="-dev$"

# default is to build html only
BUILD_TYPE=html

if [[ "$VERSION" =~ $DEV_VERSION_REGEX ]]; then
   if [[ "$CI_BRANCH" == "$MAIN_BRANCH" ]]; then
       # no need to build html, just rst
       BUILD_TYPE=rst
   fi
else
    export BUILD_DOCS_TAG="v${VERSION}"
    echo "BUILD AZP RELEASE BRANCH ${BUILD_DOCS_TAG}"
    BAZEL_BUILD_OPTIONS+=("--action_env=BUILD_DOCS_TAG")
fi

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"
IFS=" " read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTION_LIST:-}"

# We want the binary at the end
BAZEL_BUILD_OPTIONS+=(--remote_download_toplevel)

if [[ -n "${CI_TARGET_BRANCH}" ]] || [[ -n "${SPHINX_QUIET}" ]]; then
    export SPHINX_RUNNER_ARGS="-v warn"
    BAZEL_BUILD_OPTIONS+=("--action_env=SPHINX_RUNNER_ARGS")
fi

# Building html/rst is determined by then needs of CI but can be overridden in dev.
if [[ "${BUILD_TYPE}" == "html" ]] || [[ -n "${DOCS_BUILD_HTML}" ]]; then
    BUILD_HTML=1
    BUILD_HTML_TARGET="//docs:html"
    BUILD_HTML_TARBALL="bazel-bin/docs/html.tar.gz"
    if [[ -n "${CI_BRANCH}" ]] || [[ -n "${DOCS_BUILD_RELEASE}" ]]; then
        # CI build - use git sha
        BUILD_HTML_TARGET="//docs:html_release"
        BUILD_HTML_TARBALL="bazel-bin/docs/html_release.tar.gz"
    fi
fi
if [[ "${BUILD_TYPE}" == "rst" ]] || [[ -n "${DOCS_BUILD_RST}" ]]; then
    BUILD_RST=1
fi

# Build html/rst
if [[ -n "${BUILD_RST}" ]]; then
    bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" //docs:rst
fi
if [[ -n "${BUILD_HTML}" ]]; then
    bazel "${BAZEL_STARTUP_OPTIONS[@]}" build "${BAZEL_BUILD_OPTIONS[@]}" "$BUILD_HTML_TARGET"
fi

[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

# Save html/rst to output directory
if [[ -n "${BUILD_HTML}" ]]; then
    tar -xzf "$BUILD_HTML_TARBALL" -C "$DOCS_OUTPUT_DIR"
fi
if [[ -n "${BUILD_RST}" ]]; then
    cp bazel-bin/docs/rst.tar.gz "$DOCS_OUTPUT_DIR"/envoy-docs-rst.tar.gz
fi
