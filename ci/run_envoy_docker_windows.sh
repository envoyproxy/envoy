#!/bin/bash

set -e

# The image tag for the Windows image is the same as the Linux one so we use the same mechanism to find it
# shellcheck source=ci/envoy_build_sha.sh
. "$(dirname "$0")/envoy_build_sha.sh"

export HTTP_PROXY="${http_proxy:-}"
export HTTPS_PROXY="${https_proxy:-}"
DOCKER_OPTIONS=()

[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-windows2019"
# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${ENVOY_BUILD_SHA}"

ENVOY_SOURCE_DIR=$(echo "${PWD}" | sed -E "s#/([a-zA-Z])/#\1:/#")

[[ -t 1 ]] && DOCKER_OPTIONS+=(-it)

[[ -f .git ]] && [[ ! -d .git ]] && DOCKER_OPTIONS+=(-v "$(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)")

export ENVOY_BUILD_IMAGE="${IMAGE_NAME}:${IMAGE_ID}"

# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm \
       "${DOCKER_OPTIONS[@]}" \
       -e HTTP_PROXY \
       -e HTTPS_PROXY \
       -e BAZEL_BUILD_EXTRA_OPTIONS \
       -e BAZEL_EXTRA_TEST_OPTIONS \
       -e BAZEL_REMOTE_CACHE \
       -e ENVOY_STDLIB \
       -e BUILD_REASON \
       -e BAZEL_REMOTE_INSTANCE \
       -e GCP_SERVICE_ACCOUNT_KEY \
       -e NUM_CPUS \
       -e ENVOY_RBE \
       -e ENVOY_BUILD_IMAGE \
       -e ENVOY_SRCDIR \
       -e ENVOY_BUILD_TARGET \
       -e SYSTEM_PULLREQUEST_TARGETBRANCH \
       -v "${ENVOY_SOURCE_DIR}":C:/source \
       "${ENVOY_BUILD_IMAGE}" \
       bash -c "cd source && $*"
