#!/bin/bash

# Script that lists all the steps take by the CI system when doing Envoy builds. This script
# only still exists for Travis IPv6 tests.
set -e

. ./ci/envoy_build_sha.sh

# Lint travis file.
travis lint .travis.yml --skip-completion-check

# Where the Envoy build takes place.
export ENVOY_BUILD_DIR=/tmp/envoy-docker-build

function finish {
  echo "disk space at end of build:"
  df -h
}
trap finish EXIT

echo "disk space at beginning of build:"
df -h

docker run -t -i -v "$ENVOY_BUILD_DIR":/build -v $TRAVIS_BUILD_DIR:/source \
  lyft/envoy-build:$ENVOY_BUILD_SHA /bin/bash -c "cd /source && ci/do_ci.sh $TEST_TYPE"
