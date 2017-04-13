#!/bin/bash

read -r -p "Do you have master checked out with most recent changes? [y/N] " response
if [[ $response =~ ^([yY][eE][sS]|[yY])$ ]]
then
  export TAG="$(git rev-parse master)"
  docker-machine start default
  eval $(docker-machine env default)
  ./docker_build.sh
  docker login -u $DOCKER_USERNAME -p $DOCKER_PASSWORD
  docker push lyft/envoy-build:$TAG
  echo Pushed lyft/envoy-build:$TAG
  docker tag lyft/envoy-build:$TAG lyft/envoy-build:latest
  docker push lyft/envoy-build:latest
fi
