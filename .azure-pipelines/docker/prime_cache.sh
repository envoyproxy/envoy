#!/bin/bash -e

ENVOY_DOCKER_BUILD_DIR="$1"
CACHE_PATH="$2"
CACHE_ARCH="$3"

echo "Docker restored: $DOCKER_RESTORED"
echo "Bazel restored: $BAZEL_RESTORED"

if [[ -z "$CACHE_PATH" ]]; then
    echo "prime_docker_cache called without path arg" >&2
    exit 1
fi

if [[ "$CACHE_ARCH" == ".arm64" ]]; then
    CACHE_ARCH=linux/arm64
else
    CACHE_ARCH=linux/amd64
fi

DOCKER_CACHE_TARBALL="${CACHE_PATH}/docker/docker.tar.zst"
BAZEL_CACHE_TARBALL="${CACHE_PATH}/bazel/bazel.tar.zst"
BAZEL_PATH=/tmp/envoy-docker-build

echo
echo "================ Load caches ==================="
if [[ "$DOCKER_RESTORED" == "true" ]] || [[ "$BAZEL_RESTORED" == "true" ]]; then
    sudo ./.azure-pipelines/docker/load_caches.sh "$ENVOY_DOCKER_BUILD_DIR" "$CACHE_PATH" "" true
else
    sudo ./.azure-pipelines/docker/clean_docker.sh
    echo "No caches to restore"
fi
echo "==================================================="
echo

echo
echo "================ Docker fetch ======================"
if [[ "$DOCKER_RESTORED" != "true" ]]; then
    echo "Fetching Docker"
    ./ci/run_envoy_docker.sh uname -a
    docker images
else
    echo "Not fetching Docker as it was restored"
fi
echo "==================================================="
echo

echo
echo "================ Bazel fetch ======================"
# Fetch bazel dependencies
if [[ "$BAZEL_RESTORED" != "true" ]]; then
    echo "Fetching bazel"
    ./ci/run_envoy_docker.sh './ci/do_ci.sh fetch'
else
    echo "Not fetching bazel as it was restored"
fi
echo "==================================================="
echo

df -h

echo
echo "================ Save caches ======================"
# Save the caches -> tarballs
if [[ "$DOCKER_RESTORED" != "true" ]]; then
    echo "Stopping docker"
    sudo systemctl stop docker docker.socket
    sudo ./.azure-pipelines/docker/create_cache.sh "${DOCKER_CACHE_TARBALL}" . /var/lib/docker
fi

if [[ "$BAZEL_RESTORED" != "true" ]]; then
    sudo ./.azure-pipelines/docker/create_cache.sh "${BAZEL_CACHE_TARBALL}" . "${BAZEL_PATH}"
fi
sudo chmod o+r -R "${CACHE_PATH}"
echo "==================================================="
echo
