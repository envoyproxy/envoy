#!/bin/sh

set -ex

docker build -f ci/Dockerfile-envoy-image -t envoyproxy/envoy-dev:latest .
docker build -f ci/Dockerfile-envoy-alpine -t envoyproxy/envoy-alpine-dev:latest .
docker build -f ci/Dockerfile-envoy-alpine-debug -t envoyproxy/envoy-alpine-debug-dev:latest .
