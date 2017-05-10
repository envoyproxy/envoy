#!/bin/bash

set -e

[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="latest"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
docker pull lyft/envoy-build:"${IMAGE_ID}"
docker run -t -i -u $(id -u):$(id -g) -v "${ENVOY_DOCKER_BUILD_DIR}":/build \
  -v "$PWD":/source lyft/envoy-build:"${IMAGE_ID}" /bin/bash -c "cd source && $*"
