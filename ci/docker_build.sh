#!/bin/bash

set -ex

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
docker build -f ci/Dockerfile-envoy-alpine -t envoyproxy/envoy-alpine:latest .
docker build -f ci/Dockerfile-envoy-alpine-debug -t envoyproxy/envoy-alpine-debug:latest .
