#!/bin/bash -e

DOCKER_CACHE_PATH="$1"
DOCKER_BIND_PATH="$2"

if [[ -z "$DOCKER_CACHE_PATH" ]]; then
    echo "load_docker_cache called without path arg" >&2
    exit 1
fi

DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"

echo "Stopping Docker daemon ..."
systemctl stop docker docker.socket
mv /var/lib/docker/ /var/lib/docker.old
mkdir -p /var/lib/docker

if id -u vsts &> /dev/null && [[ -n "$DOCKER_BIND_PATH" ]]; then
    # use separate disk on windows hosted
    mkdir -p "$DOCKER_BIND_PATH"
    mount -o bind "$DOCKER_BIND_PATH" /var/lib/docker
fi

echo "Extracting docker cache ${DOCKER_CACHE_TARBALL} ..."
tar -I "zstd -d -T0 " -axf "$DOCKER_CACHE_TARBALL" -C /var/lib/docker
df -h
umount "${DOCKER_CACHE_PATH}"


du -ch /var/lib/docker

echo "Starting Docker daemon ..."
time systemctl start docker

journalctl --no-pager -n500 -xu docker

journalctl --no-pager -xe

docker images
df -h

# background this
rm -rf /var/lib/docker.old &
