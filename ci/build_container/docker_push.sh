#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

# push the envoy image on merge to master
branch_want_push='false'
for branch in "master"
do
   if [ "$CIRCLE_BRANCH" == "$branch" ]
   then
       branch_want_push='true'
   fi
done
if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$branch_want_push" == "true" ]
then
    diff_want_push='false'
    if [[ $(git diff HEAD^ ci/build_container/) ]]; then
        echo "There are changes in the ci/build_container directory"
        diff_want_push='true'
    elif [[ $(git diff HEAD^ bazel/) ]]; then
        echo "There are changes in the bazel directory"
        diff_want_push='true'
    fi
    if [ "$diff_want_push" == "true" ]; then
        cd ci/build_container
        docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

        for distro in ubuntu
        do
            echo "Updating envoyproxy/envoy-build-${distro} image"
            LINUX_DISTRO=$distro ./docker_build.sh
            docker push envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1"
            docker tag envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1" envoyproxy/envoy-build-"${distro}":latest
            docker push envoyproxy/envoy-build-"${distro}":latest

            if [ "$distro" == "ubuntu" ]
            then
                echo "Updating envoyproxy/envoy-build image"
                docker tag envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1" envoyproxy/envoy-build:"$CIRCLE_SHA1"
                docker push envoyproxy/envoy-build:"$CIRCLE_SHA1"
                docker tag envoyproxy/envoy-build:"$CIRCLE_SHA1" envoyproxy/envoy-build:latest
                docker push envoyproxy/envoy-build:latest
            fi
        done
    else
        echo "The ci/build_container directory has not changed"
    fi
else
    echo 'Ignoring PR branch for docker push.'
fi
