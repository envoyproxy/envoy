#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
set -e

if [ -n "$CIRCLE_TAG" ]
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
