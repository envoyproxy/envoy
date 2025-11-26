#!/usr/bin/env bash

CONFIG_FILE="$(realpath "$(dirname "${BASH_SOURCE[0]}")")/../.github/config.yml"

# Parse values from .github/config.yml
BUILD_REPO=$(awk '/^\s*repo: /{print $2}' "$CONFIG_FILE")
BUILD_SHA=$(awk '/^\s*sha: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_CI=$(awk '/^\s*sha-ci: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_DEVTOOLS=$(awk '/^\s*sha-devtools: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_DOCKER=$(awk '/^\s*sha-docker: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_GCC=$(awk '/^\s*sha-gcc: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_MOBILE=$(awk '/^\s*sha-mobile: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_WORKER=$(awk '/^\s*sha-worker: /{print $2}' "$CONFIG_FILE")

BUILD_TAG=$(awk '/^\s*tag: /{print $2}' "$CONFIG_FILE")


case $ENVOY_BUILD_VARIANT in
    ci)
        BUILD_SHA="$BUILD_SHA_CI"
        ;;
    devtools)
        BUILD_SHA="$BUILD_SHA_DEVTOOLS"
        ;;
    docker)
        BUILD_SHA="$BUILD_SHA_DOCKER"
        ;;
    gcc)
        BUILD_SHA="$BUILD_SHA_GCC"
        ;;
    mobile)
        BUILD_SHA="$BUILD_SHA_MOBILE"
        ;;
    worker)
        BUILD_SHA="$BUILD_SHA_WORKER"
        ;;
esac

# shellcheck disable=SC2034
BUILD_CONTAINER="${BUILD_REPO}@sha256:${BUILD_SHA}"


if [[ -z "$BUILD_REPO" || -z "$BUILD_SHA" || -z "$BUILD_TAG" ]]; then
    echo "Error: Missing repo, sha, or tag values in .github/config.yml"
    exit 1
fi
