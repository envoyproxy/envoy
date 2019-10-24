#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

if [[ "${AZP_BRANCH}" =~ ^refs/tags/v.* ]]; then
  CIRCLE_TAG="${AZP_BRANCH/refs\/tags\//}"
fi

if [[ -n "$CIRCLE_TAG" ]]; then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

    for BUILD_TYPE in "" "-alpine" "-alpine-debug"; do
        docker pull "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}"-dev:"$CIRCLE_SHA1"
        docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}"-dev:"$CIRCLE_SHA1" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}":"$CIRCLE_TAG"
        docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}":"$CIRCLE_TAG"
        docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}"-dev:"$CIRCLE_SHA1" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}":latest
        docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}":latest
    done
else
    echo 'Ignoring non-tag event for docker tag.'
fi
