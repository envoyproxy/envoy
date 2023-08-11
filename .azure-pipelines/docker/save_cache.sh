#!/bin/bash -e

set -o pipefail

DOCKER_CACHE_PATH="$1"
NO_MOUNT_TMPFS="${2:-}"

if [[ -z "$DOCKER_CACHE_PATH" ]]; then
    echo "prime_docker_cache called without path arg" >&2
    exit 1
fi

if [[ -e /tmp/DOCKER_CACHE_RESTORED ]]; then
    echo "Not saving cache as it was restored"
    exit 0
fi

DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"

docker images

echo "Stopping Docker ..."
systemctl stop docker

echo "Creating directory to save tarball: ${DOCKER_CACHE_PATH}"
mkdir -p "$DOCKER_CACHE_PATH"

if [[ -z "$NO_MOUNT_TMPFS" ]]; then
    echo "Mount tmpfs directory: ${DOCKER_CACHE_PATH}"
    mount -t tmpfs none "$DOCKER_CACHE_PATH"
fi

echo "Creating tarball: /var/lib/docker -> ${DOCKER_CACHE_TARBALL}"
tar cf - -C /var/lib/docker . | zstd - -T0 -o "$DOCKER_CACHE_TARBALL"

echo "Docker cache tarball created: ${DOCKER_CACHE_TARBALL}"
ls -lh "$DOCKER_CACHE_TARBALL"
