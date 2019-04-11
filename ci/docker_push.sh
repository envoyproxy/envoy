#!/bin/sh

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

if [ -n "$CIRCLE_PULL_REQUEST" ]
then
    echo 'Ignoring PR branch for docker push.'
    exit 0
fi

# push the envoy image on tags or merge to master
if [ -n "$CIRCLE_TAG" ] || [ "$CIRCLE_BRANCH" = 'master' ]
then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

    for BUILD_TYPE in "envoy" "envoy-alpine" "envoy-alpine-debug"; do
        docker push envoyproxy/"$BUILD_TYPE"-dev:latest
        docker tag envoyproxy/"$BUILD_TYPE"-dev:latest envoyproxy/"$BUILD_TYPE"-dev:"$CIRCLE_SHA1"
        docker push envoyproxy/"$BUILD_TYPE"-dev:"$CIRCLE_SHA1"
    done

    # This script tests the docker examples.
    # TODO(mattklein123): This almost always times out on CircleCI. Do not run for now until we
    # have a better CI setup.
    #./ci/verify_examples.sh
else
    echo 'Ignoring non-master branch for docker push.'
fi
