#!/bin/bash -e

DOCKER_CACHE_PATH="$1"
DOCKER_CACHE_OWNERSHIP="vsts:vsts"


if [[ -z "$DOCKER_CACHE_PATH" ]]; then
    echo "prepare_docker_cache called without path arg" >&2
    exit 1
fi

if ! id -u vsts &> /dev/null; then
    DOCKER_CACHE_OWNERSHIP=azure-pipelines
fi

# TODO(phlax): remove once https://github.com/envoyproxy/ci-infra/issues/19 is resolved
if ! command -v zstd > /dev/null && [[ -z "$NO_INSTALL_ZSTD" ]]; then
    echo "Installing zstd ..."
    sudo apt-get -qq update
    sudo apt-get -qq install -y zstd
fi

echo "Creating cache directory (${DOCKER_CACHE_PATH}) ..."
mkdir -p "${DOCKER_CACHE_PATH}"
mount -t tmpfs none "${DOCKER_CACHE_PATH}"
chown -R "$DOCKER_CACHE_OWNERSHIP" "${DOCKER_CACHE_PATH}"
