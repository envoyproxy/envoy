#!/bin/bash
echo "testing"
docker build -f ../Dockerfile -t envoy_machine_base .

#docker login -e $DOCKER_EMAIL -u $DOCKER_USERNAME -p $DOCKER_PASSWORD
#export TAG=`if [ "$TRAVIS_BRANCH" == "master" ]; then echo "latest"; else echo $TRAVIS_BRANCH ; fi`
#echo "TAG is $TAG"
#docker tag envoy_machine_base $REPO:$TAG
#docker push -f $REPO:$TAG
