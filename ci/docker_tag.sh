#!/bin/sh

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

if [ -n "$CIRCLE_TAG" ]
then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

    for BUILD_TYPE in "envoy" "envoy-alpine" "envoy-alpine-debug"; do
        docker pull envoyproxy/"$BUILD_TYPE"-dev:"$CIRCLE_SHA1"
        docker tag envoyproxy/"$BUILD_TYPE"-dev:"$CIRCLE_SHA1" envoyproxy/"$BUILD_TYPE":"$CIRCLE_TAG"
        docker push envoyproxy/"$BUILD_TYPE":"$CIRCLE_TAG"
        docker tag envoyproxy/"$BUILD_TYPE"-dev:"$CIRCLE_SHA1" envoyproxy/"$BUILD_TYPE":latest
        docker push envoyproxy/"$BUILD_TYPE":latest
    done
else
    echo 'Ignoring non-tag event for docker tag.'
fi
