#!/bin/bash
ENVOY_BUILD_SHA=e685bf07124b1aba3e6ce8efcce6a87dd7bff24f

# Script that lists all the steps take by the CI system when doing Envoy builds.
set -e

# Lint travis file.
travis lint .travis.yml --skip-completion-check

# Where the Envoy build takes place.
ENVOY_BUILD_DIR=/tmp/envoy-docker-build
mkdir -p "$ENVOY_BUILD_DIR"/build_release

# Do a build matrix with different types of builds docs, coverage, bazel.release, etc.
if [ "$TEST_TYPE" == "docs" ]
then
  echo "docs build..."
  ./docs/build.sh
  ./docs/publish.sh
  exit 0
else
  docker run -t -i -v "$ENVOY_BUILD_DIR":/build -v $TRAVIS_BUILD_DIR:/source \
    lyft/envoy-build:$ENVOY_BUILD_SHA /bin/bash -c "cd /source && ci/do_ci.sh $TEST_TYPE"
fi

if [ "$TEST_TYPE" == "bazel.release" ]
then
  cp -f "$ENVOY_BUILD_DIR"/envoy/source/exe/envoy "$ENVOY_BUILD_DIR"/build_release
  # This script builds a lyft/envoy image and pushes that image on merge to master.
  ./ci/docker_push.sh
  # This script runs on every PRs release run to test the docker examples.
  ./ci/verify_examples.sh
fi
