#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

# Setting environments for buildx tools
config_env(){
    # Qemu configurations
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

    # Remove older build instance
    docker buildx rm  multi-builder | true
    docker buildx create --use --name multi-builder --platform linux/arm64,linux/amd64
}

build_images(){
    TYPE=$1
    BUILD_TAG=$2

    # Only build/push envoyproxy/envoy multi-arch images since others still do not support.
    if [ -z "${TYPE}" ]; then
        docker buildx build --platform linux/arm64 -f ci/Dockerfile-envoy"${TYPE}" -t ${BUILD_TAG} .
        # Export envoyproxy/envoy amd64 image which will be used for building envoyproxy/envoy-google-vrp
        docker buildx build --platform linux/amd64 -f ci/Dockerfile-envoy"${TYPE}" -o type=docker -t ${BUILD_TAG} .
    elif [ "${TYPE}" == "-google-vrp" ]; then
        # The envoyproxy/envoy-google-vrp is based on envoyproxy/envoy image. So it is built from cache envoyproxy/envoy:local
        docker build -f ci/Dockerfile-envoy"${TYPE}" --cache-from "${DOCKER_IMAGE_PREFIX}:local" -t ${BUILD_TAG} .
    else
        docker build -f ci/Dockerfile-envoy"${TYPE}" -t ${BUILD_TAG} .
    fi
}

push_images(){
    TYPE=$1
    BUILD_TAG=$2

    if [ -z "${TYPE}" ]; then
        # Only push envoyproxy/envoy multi-arch images since others still do not support.
        docker buildx build --platform linux/arm64,linux/amd64 --push -f ci/Dockerfile-envoy"${TYPE}" -t ${BUILD_TAG} .
    else
        docker tag "${DOCKER_IMAGE_PREFIX}${TYPE}:local" ${BUILD_TAG}
        docker push ${BUILD_TAG}
    fi
}

# This prefix is altered for the private security images on setec builds.
DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

# "-google-vrp" must come afer "" to ensure we rebuild the local base image dependency.
BUILD_TYPES=("" "-alpine" "-alpine-debug" "-google-vrp")

# Configure docker-buildx tools
config_env

# Test the docker build in all cases, but use a local tag that we will overwrite before push in the
# cases where we do push.
for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
    build_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}:local"
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
    push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"

    # Only push latest on master builds.
    if [[ "${AZP_BRANCH}" == "${MASTER_BRANCH}" ]]; then
        push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
    fi

    # Push vX.Y-latest to tag the latest image in a release line
    if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
      RELEASE_LINE=$(echo "$IMAGE_NAME" | sed -E 's/(v[0-9]+\.[0-9]+)\.[0-9]+/\1-latest/')
        push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
    fi
done
