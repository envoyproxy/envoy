#!/bin/bash -e

set -o pipefail
ENVOY_DOCKER_BUILD_DIR="$1"
CACHE_PATH="$2"
NO_MOUNT_TMPFS="${3:-}"
CACHE_BAZEL="${4:-}"

if [[ -z "$CACHE_PATH" ]]; then
    echo "prime_docker_cache called without path arg" >&2
    exit 1
fi

DOCKER_CACHE_TARBALL="${CACHE_PATH}/docker.tar.zst"
BAZEL_CACHE_TARBALL="${CACHE_PATH}/bazel.tar.zst"

docker images

echo "Stopping Docker ..."
systemctl stop docker docker.socket

echo "Creating directory to save tarball: ${CACHE_PATH}"
mkdir -p "$CACHE_PATH"

if [[ -z "$NO_MOUNT_TMPFS" ]]; then
    echo "Mount tmpfs directory: ${CACHE_PATH}"
    mount -t tmpfs none "$CACHE_PATH"
fi

./.azure-pipelines/docker/create_cache.sh \
    "${DOCKER_CACHE_TARBALL}" \
    . \
    /var/lib/docker

if [[ "$CACHE_BAZEL" == "true" ]]; then
    ./.azure-pipelines/docker/create_cache.sh \
        "${BAZEL_CACHE_TARBALL}" \
        "${ENVOY_DOCKER_BUILD_DIR}" \
        .cache \
        bazel_root/install \
        bazel_root/base/external \
        repository_cache
fi
