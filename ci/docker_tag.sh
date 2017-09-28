#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
set -e

if [ -n "$CIRCLE_TAG" ]
then
   docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

   docker pull lyft/envoy:"$CIRCLE_SHA1"
   docker tag lyft/envoy:"$CIRCLE_SHA1" lyft/envoy:"$CIRCLE_TAG"
   docker push lyft/envoy:"$CIRCLE_TAG"

   docker pull lyft/envoy-alpine:"$CIRCLE_SHA1"
   docker tag lyft/envoy-alpine:"$CIRCLE_SHA1" lyft/envoy-alpine:"$CIRCLE_TAG"
   docker push lyft/envoy-alpine:"$CIRCLE_TAG"

   docker pull lyft/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker tag lyft/envoy-alpine-debug:"$CIRCLE_SHA1" lyft/envoy-alpine-debug:"$CIRCLE_TAG"
   docker push lyft/envoy-alpine-debug:"$CIRCLE_TAG"
else
   echo 'Ignoring non-tag event for docker tag.'
fi
