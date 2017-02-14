#!/bin/bash

# Script that lists all the steps take by the CI system when doing Envoy builds.
set -e

# Lint travis file.
travis lint .travis.yml --skip-completion-check
# Do a build matrix with different types of builds docs, coverage, normal, etc.
docker run -t -i -v $TRAVIS_BUILD_DIR:/source lyft/envoy-build:5ba9f93b749aaabdc4e6d16f941f9970a80fcf8e /bin/bash -c "cd /source && ci/do_ci.sh $TEST_TYPE"

# The following scripts are only relevant on a `normal` run.
# This script build a lyft/envoy image an that image is pushed on merge to master.
./ci/docker_push.sh
# This script runs on every PRs normal run to test the docker examples.
./ci/verify_examples.sh

# This make target publishes envoy's web site on a push to master.
make publish_docs
