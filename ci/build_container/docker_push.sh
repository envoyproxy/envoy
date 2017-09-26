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
if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$want_push" == "true" ]
then
    if [[ $(git diff HEAD^ ci/build_container/) ]]; then
        echo "There are changes in the ci/build_container directory"
        cd ci/build_container
        docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

        for distro in ubuntu centos
        do
            echo "Updating lyft/envoy-build-${distro} image"
            LINUX_DISTRO=$distro ./docker_build.sh
            docker push lyft/envoy-build-${distro}:$CIRCLE_SHA1
            docker tag lyft/envoy-build-${distro}:$CIRCLE_SHA1 lyft/envoy-build-${distro}:latest
            docker push lyft/envoy-build-${distro}:latest

            if [ "$distro" == "ubuntu" ]
            then
                echo "Updating lyft/envoy-build image"
                docker tag lyft/envoy-build-${distro}:$CIRCLE_SHA1 lyft/envoy-build:$CIRCLE_SHA1
                docker push lyft/envoy-build:$CIRCLE_SHA1
                docker tag lyft/envoy-build:$CIRCLE_SHA1 lyft/envoy-build:latest
                docker push lyft/envoy-build:latest
            fi
        done
    else
        echo "The ci/build_container directory has not changed"
    fi
else
    echo 'Ignoring PR branch for docker push.'
fi
