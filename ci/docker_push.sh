#!/bin/sh

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

# push the envoy image on merge to master
want_push='false'
for branch in "master"
do
   if [ "$CIRCLE_BRANCH" == "$branch" ]
   then
       want_push='true'
   fi
done
if [ -z "$CIRCLE_PULL_REQUEST" ] && [ -z "$CIRCLE_TAG" ] && [ "$want_push" == "true" ]
then
   docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

   docker push envoyproxy/envoy:latest
   docker tag envoyproxy/envoy:latest envoyproxy/envoy:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy:"$CIRCLE_SHA1"

   docker tag envoyproxy/envoy-alpine:latest envoyproxy/envoy-alpine:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine:latest

   docker tag envoyproxy/envoy-alpine-debug:latest envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine-debug:latest

   # This script tests the docker examples.
   # TODO(mattklein123): This almost always times out on CircleCI. Do not run for now until we
   # have a better CI setup.
   #./ci/verify_examples.sh
else
   echo 'Ignoring PR branch for docker push.'
fi
