#!/bin/bash
set -e
set -x

if [ "$TEST_TYPE" == "normal" ]
then
  # this is needed to verify the example images
  docker build -f ci/Dockerfile-envoy-image -t lyft/envoy:latest .

  # push the envoy image on merge to master
  want_push='false'
  for branch in "master"
  do
     if [ "$TRAVIS_BRANCH" == "$branch" ]
     then
         want_push='true'
     fi
  done
  if [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$want_push" == "true" ]
  then
      docker login -e $DOCKER_EMAIL -u $DOCKER_USERNAME -p $DOCKER_PASSWORD
      docker push lyft/envoy:latest
      docker tag lyft/envoy:latest lyft/envoy:$TRAVIS_COMMIT
      docker push lyft/envoy:$TRAVIS_COMMIT
      make -C ci/build_alpine_container
      docker tag lyft/envoy-alpine:latest lyft/envoy-alpine:$TRAVIS_COMMIT
      docker push lyft/envoy-alpine:$TRAVIS_COMMIT
      docker tag lyft/envoy-alpine-debug:latest lyft/envoy-alpine-debug:$TRAVIS_COMMIT
      docker push lyft/envoy-alpine-debug:$TRAVIS_COMMIT
  else
      echo 'Ignoring PR branch for docker push.'
  fi
fi
