#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

# Test the docker build in all cases, but use a local tag that we will overwrite before push in the
# cases where we do push.
for BUILD_TYPE in "" "-alpine" "-alpine-debug"; do
    docker build -f ci/Dockerfile-envoy"${BUILD_TYPE}" -t "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" .
done

# Only push images for master builds and tag builds.
if [[ "${AZP_BRANCH}" != 'refs/heads/master' ]] && ! [[ "${AZP_BRANCH}" =~ ^refs/tags/v.* ]]; then
    echo 'Ignoring non-master branch or tag for docker push.'
    exit 0
fi

if [[ "${AZP_BRANCH}" == 'refs/heads/master' ]]; then
  IMAGE_POSTFIX="-dev"
  IMAGE_NAME="$AZP_SHA1"
else
  IMAGE_POSTFIX=""
  IMAGE_NAME="${AZP_BRANCH/refs\/tags\//}"
fi

docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

for BUILD_TYPE in "" "-alpine" "-alpine-debug"; do
    docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"
    docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"

    docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
    docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
done


