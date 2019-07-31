#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

# push the envoy image on merge to master
branch_want_push='false'
for branch in "master"
do
   if [[ "$CIRCLE_BRANCH" == "$branch" ]]; then
       branch_want_push='true'
   fi
done

if [[ -z "${CIRCLE_PR_NUMBER}" && "${branch_want_push}" == "true" ]]; then
    diff_base="HEAD^"
else
    git fetch https://github.com/envoyproxy/envoy.git master
    diff_base="$(git merge-base HEAD FETCH_HEAD)"
fi

diff_want_build='false'
if [[ ! -z $(git diff --name-only "${diff_base}..HEAD" ci/build_container/) ]]; then
    echo "There are changes in the ci/build_container directory"
    diff_want_build='true'
fi

cd ci/build_container
if [ "$diff_want_build" == "true" ]; then
    for distro in ubuntu centos
    do
        echo "Updating envoyproxy/envoy-build-${distro} image"
        LINUX_DISTRO=$distro ./docker_build.sh
    done
else
    echo "The ci/build_container directory has not changed"
    exit 0
fi

if [[ -z "${CIRCLE_PR_NUMBER}" && "${branch_want_push}" == "true" ]]; then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

    if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
        echo ${GCP_SERVICE_ACCOUNT_KEY} | base64 --decode | gcloud auth activate-service-account --key-file=-
        gcloud auth configure-docker
    fi

    for distro in ubuntu centos
    do
        echo "Updating envoyproxy/envoy-build-${distro} image"
        docker push envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1"
        docker tag envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1" envoyproxy/envoy-build-"${distro}":latest
        docker push envoyproxy/envoy-build-"${distro}":latest

        if [[ "$distro" == "ubuntu" ]]
        then
            echo "Updating envoyproxy/envoy-build image"
            docker tag envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1" envoyproxy/envoy-build:"$CIRCLE_SHA1"
            docker push envoyproxy/envoy-build:"$CIRCLE_SHA1"
            docker tag envoyproxy/envoy-build:"$CIRCLE_SHA1" envoyproxy/envoy-build:latest
            docker push envoyproxy/envoy-build:latest

            echo "Updating gcr.io/envoy-ci/envoy-build image"
            docker tag envoyproxy/envoy-build-"${distro}":"$CIRCLE_SHA1" gcr.io/envoy-ci/envoy-build:"$CIRCLE_SHA1"
            docker push gcr.io/envoy-ci/envoy-build:"$CIRCLE_SHA1"
        fi
    done
else
    echo 'Ignoring PR branch for docker push.'
fi
