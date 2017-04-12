#!/bin/bash

set -e

BUILD_DIR=/tmp/envoy-docker-build
mkdir -p "${BUILD_DIR}"
docker pull lyft/envoy-build:latest
docker run -t -i -u $(id -u):$(id -g) -v "${BUILD_DIR}":/build \
  -v "$PWD":/source lyft/envoy-build:latest /bin/bash -c "cd source && $*"
