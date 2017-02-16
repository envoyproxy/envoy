#!/bin/bash

# Script that lists all the steps take by the CI system when doing Envoy builds.
set -e

# Lint travis file.
travis lint .travis.yml --skip-completion-check
# Do a build matrix with different types of builds docs, coverage, normal, etc.
docker run -t -i -v $TRAVIS_BUILD_DIR:/source lyft/envoy-build:d4610d5d7ac01b275612b14f0fbef72b1b374d87 /bin/bash -c "cd /source && ci/do_ci.sh $TEST_TYPE $TRAVIS_PULL_REQUEST $TRAVIS_BRANCH"

# The following scripts are only relevant on a `normal` run.
# This script build a lyft/envoy image an that image is pushed on merge to master.
./ci/docker_push.sh
# This script runs on every PRs normal run to test the docker examples.
./ci/verify_examples.sh
