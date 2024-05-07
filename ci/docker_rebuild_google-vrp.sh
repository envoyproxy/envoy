#!/usr/bin/env bash

# Script to rebuild Dockerfile-envoy-google-vrp locally (i.e. not in CI) for development purposes.
# This makes use of the latest envoy:dev base image on Docker Hub as the base and takes an
# optional local path for an Envoy binary. When a custom local Envoy binary is used, the script
# switches to using ${BASE_DOCKER_IMAGE} for the build, which should be configured to provide
# compatibility with your local build environment (specifically glibc).
#
# Usage:
#
# Basic rebuild of Docker image (tagged envoy-google-vrp:local):
#
#   ./ci/docker_rebuild_google-vrp.sh
#
# Basic rebuild of Docker image (tagged envoy-google-vrp:local) with some local Envoy binary:
#
#   bazel build //source/exe:envoy-static --config=libc++ -copt
#   ./ci/docker_rebuild_google-vrp.sh bazel-bin/source/exe/envoy-static

set -e

# Don't use the local envoy:dev, but pull from Docker Hub instead, this avoids having to rebuild
# this local dep which is fairly stable.
BASE_DOCKER_IMAGE="envoyproxy/envoy:dev"
declare -r DOCKER_BUILD_FILE="ci/Dockerfile-envoy"

DOCKER_CONTEXT=.
DOCKER_BUILD_ARGS=(
    -f "${DOCKER_BUILD_FILE}"
    -t "envoy-google-vrp:local"
    --build-arg "ENVOY_VRP_BASE_IMAGE=${BASE_DOCKER_IMAGE}")

# In CI we specifically do _not_ test against the published images, so prevent
# Docker from pulling unless required.
if [[ -z "$DOCKER_NO_PULL" ]]; then
    DOCKER_BUILD_ARGS+=(--pull)
fi

# If we specify a local Envoy binary, copy it into the context, and use the custom recipe
if [[ -n "$1" ]]; then
    declare -r LOCAL_ENVOY="envoy-binary"
    declare -r LOCAL_ENVOY_DIR="build_envoy"
    ENVOY_CTX_BINARY_PATH="${LOCAL_ENVOY_DIR}/${LOCAL_ENVOY}"

    # This should match your local machine if you are building custom Envoy binaries outside of Docker.
    # This provides compatibility of locally built Envoy and glibc in the Docker env.
    BASE_DOCKER_IMAGE="ubuntu:20.04"
    # Copy the binary to deal with symlinks in Bazel cache and Docker daemon confusion.
    mkdir -p "${LOCAL_ENVOY_DIR}"
    cp -f "$1" "${ENVOY_CTX_BINARY_PATH}"
    DOCKER_BUILD_ARGS+=(
        --target envoy-google-vrp-custom
        --build-arg ENVOY_CTX_BINARY_PATH="$ENVOY_CTX_BINARY_PATH")
else
    DOCKER_BUILD_ARGS+=(--target envoy-google-vrp)
fi

DOCKER_BUILDKIT=1 docker build "${DOCKER_BUILD_ARGS[@]}" "$DOCKER_CONTEXT"

if [[ -n "$1" ]]; then
    rm -rf "${LOCAL_ENVOY_DIR}"
fi
