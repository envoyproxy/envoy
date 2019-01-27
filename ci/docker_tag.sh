#!/bin/sh

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

if [ -n "$CIRCLE_TAG" ]
then
   docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

   docker pull envoyproxy/envoy:"$CIRCLE_SHA1"
   docker tag envoyproxy/envoy:"$CIRCLE_SHA1" envoyproxy/envoy:"$CIRCLE_TAG"
   docker push envoyproxy/envoy:"$CIRCLE_TAG"

   docker pull envoyproxy/envoy-alpine:"$CIRCLE_SHA1"
   docker tag envoyproxy/envoy-alpine:"$CIRCLE_SHA1" envoyproxy/envoy-alpine:"$CIRCLE_TAG"
   docker push envoyproxy/envoy-alpine:"$CIRCLE_TAG"

   docker pull envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker tag envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1" envoyproxy/envoy-alpine-debug:"$CIRCLE_TAG"
   docker push envoyproxy/envoy-alpine-debug:"$CIRCLE_TAG"
else
   echo 'Ignoring non-tag event for docker tag.'
fi
