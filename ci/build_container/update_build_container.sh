#!/bin/bash
set -e

cd ci/build_container
./docker_build.sh
docker login -u $DOCKERHUB_USERNAME -p $DOCKERHUB_PASSWORD
docker push lyft/envoy-build:$TRAVIS_COMMIT
docker tag lyft/envoy-build:$TRAVIS_COMMIT lyft/envoy-build:latest
docker push lyft/envoy-build:latest
