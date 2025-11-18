#!/usr/bin/env bash

CONFIG_FILE="$(realpath "$(dirname "${BASH_SOURCE[0]}")")/../.github/config.yml"

# Parse values from .github/config.yml
BUILD_REPO=$(awk '/^\s*repo: /{print $2}' "$CONFIG_FILE")
BUILD_SHA=$(awk '/^\s*sha: /{print $2}' "$CONFIG_FILE")
BUILD_TAG=$(awk '/^\s*tag: /{print $2}' "$CONFIG_FILE")
BUILD_CONTAINER="${BUILD_REPO}:${BUILD_TAG}@sha256:${BUILD_SHA}"

# For backward compat
ENVOY_BUILD_CONTAINER="$BUILD_CONTAINER"

if [[ -z "$BUILD_REPO" || -z "$BUILD_SHA" || -z "$BUILD_TAG" || -z "$ENVOY_BUILD_CONTAINER" ]]; then
    echo "Error: Missing repo, sha, or tag values in .github/config.yml"
    exit 1
fi
