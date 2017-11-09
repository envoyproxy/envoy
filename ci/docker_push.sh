#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
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
   # TODO(mattklein123): Currently we are doing this push in the context of the release job which
   # happens inside of our build image. We should switch to using Circle caching so each of these
   # are discrete jobs that work with the binary. All of these commands run on a remote docker
   # server also so we have to temporarily install docker here.
   # https://circleci.com/docs/2.0/building-docker-images/
   VER="17.03.0-ce"
   curl -L -o /tmp/docker-"$VER".tgz https://get.docker.com/builds/Linux/x86_64/docker-"$VER".tgz
   tar -xz -C /tmp -f /tmp/docker-"$VER".tgz
   mv /tmp/docker/* /usr/bin

   docker build -f ci/Dockerfile-envoy-image -t envoyproxy/envoy:latest .
   docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"
   docker push envoyproxy/envoy:latest
   docker tag envoyproxy/envoy:latest envoyproxy/envoy:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy:"$CIRCLE_SHA1"

   docker build -f ci/Dockerfile-envoy-alpine -t envoyproxy/envoy-alpine:latest .
   docker tag envoyproxy/envoy-alpine:latest envoyproxy/envoy-alpine:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine:latest

   docker build -f ci/Dockerfile-envoy-alpine-debug -t envoyproxy/envoy-alpine-debug:latest .
   docker tag envoyproxy/envoy-alpine-debug:latest envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine-debug:"$CIRCLE_SHA1"
   docker push envoyproxy/envoy-alpine-debug:latest

   # This script tests the docker examples.
   # TODO(mattklein123): This almost always times out on Travis. Do not run for now until we
   # have a better CI setup.
   #./ci/verify_examples.sh
else
   echo 'Ignoring PR branch for docker push.'
fi
