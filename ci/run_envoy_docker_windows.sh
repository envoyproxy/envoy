#!/bin/bash

set -e

# TODO(sunjayBhatia, wrowe): update this note once we have an RBE toolchain generated for Windows
# NOTE: Update this from the latest pushed image here: https://hub.docker.com/r/envoyproxy/envoy-build-windows2019/tags
ENVOY_BUILD_SHA="3cbc11e373dc4e3a523b9273ed010c5e0f197874"

[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-windows2019"
# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${ENVOY_BUILD_SHA}"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=$(echo "${TEMP}/envoy-docker-build" | sed "s#/c/#C:/#")

ENVOY_SOURCE_DIR=$(echo "${PWD}" | sed "s#/c/#C:/#")

[[ -f .git ]] && [[ ! -d .git ]] && GIT_VOLUME_OPTION="-v $(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)"

[[ -t 1 ]] && DOCKER_TTY_OPTION=-it

export ENVOY_BUILD_IMAGE="${IMAGE_NAME}:${IMAGE_ID}"

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm ${DOCKER_TTY_OPTION} -e HTTP_PROXY=${http_proxy} -e HTTPS_PROXY=${https_proxy} \
  -v "${ENVOY_DOCKER_BUILD_DIR}":C:/build ${GIT_VOLUME_OPTION} -e BAZEL_BUILD_EXTRA_OPTIONS \
  -e BAZEL_EXTRA_TEST_OPTIONS -e BAZEL_REMOTE_CACHE -e ENVOY_STDLIB -e BUILD_REASON \
  -e BAZEL_REMOTE_INSTANCE -e GCP_SERVICE_ACCOUNT_KEY -e NUM_CPUS -e ENVOY_RBE -e ENVOY_BUILD_IMAGE \
  -e ENVOY_SRCDIR -e ENVOY_BUILD_TARGET -e SYSTEM_PULLREQUEST_TARGETBRANCH -v ${ENVOY_SOURCE_DIR}:C:/source \
  "${ENVOY_BUILD_IMAGE}" \
  bash -c "cd source && $*"
