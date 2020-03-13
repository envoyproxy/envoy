#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

if [[ -n "$CIRCLE_PULL_REQUEST" ]]; then
    echo 'Ignoring PR branch for docker push.'
    exit 0
fi

DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"

# push the envoy image on tags or merge to master
if [[ "${AZP_BRANCH}" == 'refs/heads/master' ]] || [[ "${AZP_BRANCH}" =~ ^refs/heads/release/v.* ]]; then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

    for BUILD_TYPE in "" "-alpine" "-alpine-debug"; do
        docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}-dev:${CIRCLE_SHA1}"
        if [[ "$AZP_BRANCH" == 'refs/heads/master' ]]; then
            docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}-dev:${CIRCLE_SHA1}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}-dev:latest"
            docker push "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}-dev:latest"
        fi
    done

    # This script tests the docker examples.
    # TODO(mattklein123): This almost always times out on CircleCI. Do not run for now until we
    # have a better CI setup.
    #./ci/verify_examples.sh
else
    echo 'Ignoring non-master branch for docker push.'
fi
