#!/bin/bash -e

DOCKER_CACHE_PATH="$1"
DOCKER_CACHE_ARCH="$2"

if [[ -z "$DOCKER_CACHE_PATH" ]]; then
    echo "prime_docker_cache called without path arg" >&2
    exit 1
fi

if [[ "$DOCKER_CACHE_ARCH" == ".arm64" ]]; then
    DOCKER_CACHE_ARCH=linux/arm64
else
    DOCKER_CACHE_ARCH=linux/amd64
fi

DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"

echo "Stopping Docker ..."
systemctl stop docker

echo "Restarting Docker with empty /var/lib/docker ..."
mv /var/lib/docker/ /var/lib/docker.old
mkdir /var/lib/docker
systemctl start docker

BUILD_IMAGE=$(head -n1 .devcontainer/Dockerfile  | cut -d: -f2)

echo "Pulling build image for ${DOCKER_CACHE_ARCH} (${BUILD_IMAGE}) ..."
docker pull -q --platform "${DOCKER_CACHE_ARCH}" "envoyproxy/envoy-build-ubuntu:${BUILD_IMAGE}"

echo "Stopping docker"
systemctl stop docker

echo "Exporting /var/lib/docker -> ${DOCKER_CACHE_PATH}"
mkdir -p "$DOCKER_CACHE_PATH"
tar -I "zstd -T0 --fast " -acf "$DOCKER_CACHE_TARBALL" -C /var/lib/docker .

echo "Docker cache tarball created: ${DOCKER_CACHE_TARBALL}"
ls -lh "$DOCKER_CACHE_TARBALL"
