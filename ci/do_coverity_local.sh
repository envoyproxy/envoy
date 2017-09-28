#!/bin/bash
#
#  do_coverity_local.sh
#
#  This script builds Envoy with the Coverity Scan Built Tool.
#
#  It expects the following environment variables to be set:
#    COVERITY_SCAN_DIR   - set to the directory where the Coverity Scan Build Tool resides.
#    COVERITY_TOKEN      - set to the user's Coverity Scan project token.
#    COVERITY_USER_EMAIL - set to the email address used with the Coverity account.
#                          defaults to the local git config user.email.


set -e

. ./ci/envoy_build_sha.sh

[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build
TEST_TYPE="bazel.coverity"
COVERITY_USER_EMAIL=${COVERITY_USER_EMAIL:-$(git config user.email)}
COVERITY_OUTPUT_FILE="${ENVOY_DOCKER_BUILD_DIR}"/envoy/source/exe/envoy-coverity-output.tgz

if [ -z "$COVERITY_SCAN_DIR" ]
then
  echo "Error: COVERITY_SCAN_DIR environment variable not set."
  echo "Unable to locate Coverity Scan."
  echo "Install Coverity Scan Build Tool and set COVERITY_SCAN_DIR."
  echo "https://scan.coverity.com/download"
  exit 1
else
  export COVERITY_MOUNT="-v ${COVERITY_SCAN_DIR}:/coverity"
fi

ci/run_envoy_docker.sh "ci/do_ci.sh ${TEST_TYPE}"

if [ -z "$COVERITY_TOKEN" ]
then
  echo "COVERITY_TOKEN environment variable not set."
  echo "Manually submit build result to https://scan.coverity.com/projects/envoy-proxy/builds/new."
elif [[ $(find "${COVERITY_OUTPUT_FILE}" -type f -size +256M 2>/dev/null) ]]
then
  echo "Uploading Coverity Scan build"
  curl \
    --form token="${COVERITY_TOKEN}" \
    --form email="${COVERITY_USER_EMAIL}" \
    --form file=@"${COVERITY_OUTPUT_FILE}" \
    --form version="${ENVOY_BUILD_SHA}" \
    --form description="Envoy Proxy Build ${ENVOY_BUILD_SHA}" \
    https://scan.coverity.com/builds?project=Envoy+Proxy

else
  echo "Coverity Scan output file appears to be too small."
  echo "Not submitting build for analysis."
  exit 1
fi
