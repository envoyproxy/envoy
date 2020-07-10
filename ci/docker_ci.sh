#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

# This prefix is altered for the private security images on setec builds.
DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

# "-google-vrp" must come afer "" to ensure we rebuild the local base image dependency.
BUILD_TYPES=("" "-alpine" "-alpine-debug" "-google-vrp")

# Test the docker build in all cases, but use a local tag that we will overwrite before push in the
# cases where we do push.
for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
    docker build -f ci/Dockerfile-envoy"${BUILD_TYPE}" -t "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" .
done

MASTER_BRANCH="refs/heads/master"
RELEASE_BRANCH_REGEX="^refs/heads/release/v.*"
RELEASE_TAG_REGEX="^refs/tags/v.*"

# Only push images for master builds, release branch builds, and tag builds.
if [[ "${AZP_BRANCH}" != "${MASTER_BRANCH}" ]] && \
   ! [[ "${AZP_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]] && \
   ! [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    echo 'Ignoring non-master branch or tag for docker push.'
    exit 0
fi

# For master builds and release branch builds use the dev repo. Otherwise we assume it's a tag and
# we push to the primary repo.
if [[ "${AZP_BRANCH}" == "${MASTER_BRANCH}" ]] || \
   [[ "${AZP_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]]; then
  IMAGE_POSTFIX="-dev"
  IMAGE_NAME="$AZP_SHA1"
else
  IMAGE_POSTFIX=""
  IMAGE_NAME="${AZP_BRANCH/refs\/tags\//}"
fi

docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
    docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"
    docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"

    # Only push latest on master builds.
    if [[ "${AZP_BRANCH}" == "${MASTER_BRANCH}" ]]; then
        docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
        docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
    fi

    # Push vX.Y-latest to tag the latest image in a release line
    if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
      RELEASE_LINE=$(echo "$IMAGE_NAME" | sed -E 's/(v[0-9]+\.[0-9]+)\.[0-9]+/\1-latest/')
      docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
      docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
    fi
done


