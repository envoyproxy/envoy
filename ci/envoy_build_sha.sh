#!/usr/bin/env bash

CONFIG_FILE="$(realpath "$(dirname "${BASH_SOURCE[0]}")")/../.github/config.yml"

# Parse values from .github/config.yml
BUILD_REPO=$(awk '/^\s*repo: /{print $2}' "$CONFIG_FILE")
BUILD_SHA=$(awk '/^\s*sha: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_DOCKER=$(awk '/^\s*sha-docker: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_MOBILE=$(awk '/^\s*sha-mobile: /{print $2}' "$CONFIG_FILE")

BUILD_TAG=$(awk '/^\s*tag: /{print $2}' "$CONFIG_FILE")


case $ENVOY_BUILD_VARIANT in
    docker)
        BUILD_SHA="$BUILD_SHA_DOCKER"
        ;;
    mobile)
        BUILD_SHA="$BUILD_SHA_MOBILE"
        ;;
esac

# shellcheck disable=SC2034
BUILD_CONTAINER="${BUILD_REPO}@sha256:${BUILD_SHA}"


if [[ -z "$BUILD_REPO" || -z "$BUILD_SHA" || -z "$BUILD_TAG" ]]; then
    echo "Error: Missing repo, sha, or tag values in .github/config.yml"
    exit 1
fi
