#!/bin/bash

set -ex

DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

docker build -f ci/Dockerfile-envoy-image -t "${DOCKER_IMAGE_PREFIX}-dev:${CIRCLE_SHA1}" .
docker build -f ci/Dockerfile-envoy-alpine -t "${DOCKER_IMAGE_PREFIX}-alpine-dev:${CIRCLE_SHA1}" .
docker build -f ci/Dockerfile-envoy-alpine-debug -t "${DOCKER_IMAGE_PREFIX}-alpine-debug-dev:${CIRCLE_SHA1}" .
