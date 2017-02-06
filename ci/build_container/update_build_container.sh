#!/bin/bash
set -ev
read -r -p "Are you on master and have a clean branch? [y/N] " response
if [[ $response =~ ^([yY][eE][sS]|[yY])$ ]]
then
  read -r -p "What SHA do you want to tag the envoy-build with? [y/N] " tag
  docker-machine start default
  eval $(docker-machine env default)
  docker build --rm -t lyft/envoy-build:$tag .
  docker login -u $DOCKER_USERNAME -p $DOCKER_PASSWORD
  docker push lyft/envoy-build:$tag
fi
