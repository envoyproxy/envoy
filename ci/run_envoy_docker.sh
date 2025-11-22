#!/usr/bin/env bash

set -e

# TODO(phlax): Add a check that a usable version of docker compose is available


SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source build SHA information
# shellcheck source=ci/envoy_build_sha.sh
source "${SCRIPT_DIR}/envoy_build_sha.sh"

# User/group IDs
USER_UID="$(id -u)"
USER_GID="$(id -g)"
export USER_UID
export USER_GID

# These should probably go in users .env as docker compose will pick that up
export HTTP_PROXY="${HTTP_PROXY:-${http_proxy:-}}"
export HTTPS_PROXY="${HTTPS_PROXY:-${https_proxy:-}}"
export NO_PROXY="${NO_PROXY:-${no_proxy:-}}"
export GOPROXY="${GOPROXY:-${go_proxy:-}}"

# Docker-in-Docker handling
if [[ -n "$ENVOY_DOCKER_IN_DOCKER" ]]; then
    DOCKER_GID="$(stat -c %g /var/run/docker.sock 2>/dev/null || echo "$USER_GID")"
    export DOCKER_GID
fi

if [[ -n "$ENVOY_DOCKER_IN_DOCKER" || -n "$ENVOY_SHARED_TMP_DIR" ]]; then
    export SHARED_TMP_DIR="${ENVOY_SHARED_TMP_DIR:-/tmp/bazel-shared}"
    mkdir -p "${SHARED_TMP_DIR}"
    chmod 777 "${SHARED_TMP_DIR}"
fi

if [[ -n "$ENVOY_DOCKER_PLATFORM" ]]; then
    echo "Setting Docker platform: ${ENVOY_DOCKER_PLATFORM}"
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
fi

ENVOY_BUILD_IMAGE="${ENVOY_BUILD_IMAGE:-${BUILD_CONTAINER}}"

export DOCKER_COMMAND="${*:-bash}"
export ENVOY_BUILD_IMAGE

COMPOSE_SERVICE="envoy-build"
if [[ -n "$MOUNT_GPG_HOME" ]]; then
    COMPOSE_SERVICE="envoy-build-gpg"
elif [[ -n "$ENVOY_DOCKER_IN_DOCKER" ]]; then
    COMPOSE_SERVICE="envoy-build-dind"
fi

exec docker compose \
    -f "${SCRIPT_DIR}/docker-compose.yml" \
    ${ENVOY_DOCKER_PLATFORM:+-p "$ENVOY_DOCKER_PLATFORM"} \
    run \
    --rm \
    "${COMPOSE_SERVICE}"
