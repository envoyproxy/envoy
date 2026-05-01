#!/usr/bin/env bash

set -e -o pipefail

. ci/envoy_build_sha.sh

echo "Building devcontainer: ${BUILD_CONTAINER}"

sed -E "s|^FROM .*|FROM ${BUILD_CONTAINER}|" .devcontainer/Dockerfile.in > .devcontainer/Dockerfile
