#!/bin/bash -e

DOCKER_CACHE_PATH="$1"

if [[ -z "$DOCKER_CACHE_PATH" ]]; then
    echo "prime_docker_cache called without path arg" >&2
    exit 1
fi

DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"

systemctl stop docker
mv /var/lib/docker/ /var/lib/docker.old
mkdir /var/lib/docker
systemctl start docker
BUILD_IMAGE=$(head -n1 .devcontainer/Dockerfile  | cut -d: -f2)
docker pull "envoyproxy/envoy-build-ubuntu:${BUILD_IMAGE}"
docker images
systemctl stop docker
mkdir -p "$DOCKER_CACHE_PATH"
tar -I "zstd -T0 --fast " -acf "$DOCKER_CACHE_TARBALL" -C /var/lib/docker .
echo "Docker cache tarball created: ${DOCKER_CACHE_TARBALL}"
ls -lh "$DOCKER_CACHE_TARBALL"
