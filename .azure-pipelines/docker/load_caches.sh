#!/bin/bash -e

ENVOY_DOCKER_BUILD_DIR="$1"
CACHE_PATH="$2"
DOCKER_BIND_PATH="$3"
DOCKER_NO_TMPFS="$4"


if [[ -z "$CACHE_PATH" ]]; then
    echo "load_caches called without path arg" >&2
    exit 1
fi

DOCKER_CACHE_PATH="${CACHE_PATH}/docker"
DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"

BAZEL_CACHE_PATH="${CACHE_PATH}/bazel"
BAZEL_CACHE_TARBALL="${BAZEL_CACHE_PATH}/bazel.tar.zst"

echo "Stopping Docker daemon ..."
systemctl stop docker docker.socket

mv /var/lib/docker/ /var/lib/docker.old
mkdir -p /var/lib/docker

if id -u vsts &> /dev/null && [[ -n "$DOCKER_BIND_PATH" ]]; then
    # use separate disk on windows hosted
    echo "Binding docker directory ${DOCKER_BIND_PATH} -> /var/lib/docker ..."
    mkdir -p "$DOCKER_BIND_PATH"
    mount -o bind "$DOCKER_BIND_PATH" /var/lib/docker
elif ! id -u vsts &> /dev/null && [[ -z "$DOCKER_NO_TMPFS" ]]; then
    echo "Mounting tmpfs directory -> /var/lib/docker ..."
    # Use a ramdisk to load docker (avoids Docker slow start on big disk)
    mount -t tmpfs none /var/lib/docker
else
    # If we are on a managed/resource-constrained host but the bind path is not set then we need to remove
    # the old /var/lib/docker to free some space (maybe)
    DOCKER_REMOVE_EXISTING=1
fi

if [[ -e "${DOCKER_CACHE_TARBALL}" ]]; then
    echo "Extracting docker cache ${DOCKER_CACHE_TARBALL} -> /var/lib/docker ..."
    zstd --stdout -d "$DOCKER_CACHE_TARBALL" | tar --warning=no-timestamp -xf - -C /var/lib/docker
    touch /tmp/DOCKER_CACHE_RESTORED
else
    echo "No Docker cache to restore, starting Docker with no data"
fi

echo "Starting Docker daemon ..."
systemctl start docker

if mountpoint -q "${DOCKER_CACHE_PATH}"; then
    echo "Unmount cache tmp ${DOCKER_CACHE_PATH} ..."
    umount "${DOCKER_CACHE_PATH}"
else
    echo "Remove cache tmp ${DOCKER_CACHE_PATH} ..."
    rm -rf "${DOCKER_CACHE_PATH}"
fi
docker images

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"

if [[ -e "${BAZEL_CACHE_TARBALL}" ]]; then
    echo "Extracting bazel cache ${BAZEL_CACHE_TARBALL} -> ${ENVOY_DOCKER_BUILD_DIR} ..."
    zstd --stdout -d "$BAZEL_CACHE_TARBALL" | tar --warning=no-timestamp -xf - -C "${ENVOY_DOCKER_BUILD_DIR}"
else
    echo "No bazel cache to restore, starting bazel with no data"
fi

if mountpoint -q "${CACHE_PATH}"; then
    echo "Unmount cache tmp ${CACHE_PATH} ..."
    umount "${CACHE_PATH}"
else
    echo "Remove cache tmp ${CACHE_PATH} ..."
    rm -rf "${CACHE_PATH}"
fi

# this takes time but may be desirable in some situations
if [[ -n "$DOCKER_REMOVE_EXISTING" ]]; then
    rm -rf /var/lib/docker.old
fi

df -h
