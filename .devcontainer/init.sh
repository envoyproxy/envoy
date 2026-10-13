#!/usr/bin/env bash

set -e -o pipefail

. ci/envoy_build_sha.sh

echo "Building devcontainer: ${BUILD_CONTAINER}"

sed "s|%%ENVOY_BUILD_IMAGE%%|${BUILD_CONTAINER}|g" .devcontainer/Dockerfile.in > .devcontainer/Dockerfile
