#!/usr/bin/env bash

CONFIG_FILE="$(realpath "$(dirname "${BASH_SOURCE[0]}")")/../.github/config.yml"

# Parse values from .github/config.yml
BUILD_REPO=$(awk '/^[ ]*repo: /{print $2}' "$CONFIG_FILE")
BUILD_SHA=$(awk '/^[ ]*sha: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_CI=$(awk '/^[ ]*sha-ci: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_DEVTOOLS=$(awk '/^[ ]*sha-devtools: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_DOCKER=$(awk '/^[ ]*sha-docker: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_GCC=$(awk '/^[ ]*sha-gcc: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_MOBILE=$(awk '/^[ ]*sha-mobile: /{print $2}' "$CONFIG_FILE")
BUILD_SHA_WORKER=$(awk '/^[ ]*sha-worker: /{print $2}' "$CONFIG_FILE")

BUILD_TAG=$(awk '/^[ ]*tag: /{print $2}' "$CONFIG_FILE")


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

# Validate extracted values contain only expected characters
if [[ -n "$BUILD_REPO" && ! "$BUILD_REPO" =~ ^[a-zA-Z0-9._:/-]+$ ]]; then
    echo "Error: build-image.repo contains invalid characters"
    exit 1
fi
if [[ -n "$BUILD_SHA" && ! "$BUILD_SHA" =~ ^[a-fA-F0-9]+$ ]]; then
    echo "Error: build-image.sha contains invalid characters"
    exit 1
fi
if [[ -n "$BUILD_TAG" && ! "$BUILD_TAG" =~ ^[a-zA-Z0-9._-]+$ ]]; then
    echo "Error: build-image.tag contains invalid characters"
    exit 1
fi

# shellcheck disable=SC2034
BUILD_CONTAINER="${BUILD_REPO}@sha256:${BUILD_SHA}"

if [[ -z "$BUILD_REPO" || -z "$BUILD_SHA" || -z "$BUILD_TAG" ]]; then
    echo "Error: Missing repo, sha, or tag values in .github/config.yml"
    exit 1
fi
