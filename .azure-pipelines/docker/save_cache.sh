#!/bin/bash -e

DOCKER_CACHE_PATH="$1"

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

echo "Creating tmpfs directory to save tarball: ${DOCKER_CACHE_PATH}"
mkdir -p "$DOCKER_CACHE_PATH"
mount -t tmpfs none "$DOCKER_CACHE_PATH"

echo "Creating tarball: /var/lib/docker -> ${DOCKER_CACHE_TARBALL}"
tar -I "zstd -T0 --fast " -acf "$DOCKER_CACHE_TARBALL" -C /var/lib/docker .

echo "Docker cache tarball created: ${DOCKER_CACHE_TARBALL}"
ls -lh "$DOCKER_CACHE_TARBALL"
