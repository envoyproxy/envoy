#!/bin/bash

set -e

[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="latest"

BUILD_DIR=/tmp/envoy-docker-build
mkdir -p "${BUILD_DIR}"
docker pull lyft/envoy-build:"${IMAGE_ID}"
docker run -t -i -u $(id -u):$(id -g) -v "${BUILD_DIR}":/build \
  -v "$PWD":/source lyft/envoy-build:"${IMAGE_ID}" /bin/bash -c "cd source && $*"
