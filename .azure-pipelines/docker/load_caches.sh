#!/bin/bash -e

ENVOY_DOCKER_BUILD_DIR="$1"
CACHE_PATH="$2"
DOCKER_BIND_PATH="$3"
DOCKER_NO_TMPFS="$4"


if [[ -z "$CACHE_PATH" ]]; then
    echo "load_caches called without path arg" >&2
    exit 1
fi

if [[ -e "${CACHE_PATH}/all" ]]; then
    DOCKER_CACHE_PATH="${CACHE_PATH}/all"
    BAZEL_CACHE_PATH="${CACHE_PATH}/all"
else
    DOCKER_CACHE_PATH="${CACHE_PATH}/docker"
    BAZEL_CACHE_PATH="${CACHE_PATH}/bazel"
fi

DOCKER_CACHE_TARBALL="${DOCKER_CACHE_PATH}/docker.tar.zst"
BAZEL_CACHE_TARBALL="${BAZEL_CACHE_PATH}/bazel.tar.zst"


remount_docker () {
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
}

extract_docker () {
    if [[ -e "${DOCKER_CACHE_TARBALL}" ]]; then
        echo "Extracting docker cache ${DOCKER_CACHE_TARBALL} -> /var/lib/docker ..."
        zstd --stdout -d "$DOCKER_CACHE_TARBALL" | tar --warning=no-timestamp -xf - -C /var/lib/docker
    else
        echo "No Docker cache to restore, starting Docker with no data"
    fi
}

extract_bazel () {
    if [[ -e "${BAZEL_CACHE_TARBALL}" ]]; then
        echo "Extracting bazel cache ${BAZEL_CACHE_TARBALL} -> ${ENVOY_DOCKER_BUILD_DIR} ..."
        zstd --stdout -d "$BAZEL_CACHE_TARBALL" | tar --warning=no-timestamp -xf - -C "${ENVOY_DOCKER_BUILD_DIR}"
        if id -u vsts &> /dev/null; then
            sudo chown -R vsts:vsts "${ENVOY_DOCKER_BUILD_DIR}"
        else
            sudo chown -R azure-pipelines:azure-pipelines "${ENVOY_DOCKER_BUILD_DIR}"
        fi
    else
        echo "No bazel cache to restore, starting bazel with no data"
    fi
}

cleanup_cache () {
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
}

restart_docker () {
    echo "Starting Docker daemon ..."
    systemctl start docker
    docker images
    mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
}

df -h

remount_docker
extract_bazel
extract_docker
restart_docker
cleanup_cache

df -h
