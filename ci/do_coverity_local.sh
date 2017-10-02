#!/bin/bash
#
#  do_coverity_local.sh
#
#  This script builds Envoy with the Coverity Scan Built Tool.
#
#  It expects the following environment variables to be set:
#    COVERITY_TOKEN      - set to the user's Coverity Scan project token.
#    COVERITY_USER_EMAIL - set to the email address used with the Coverity account.
#                          defaults to the local git config user.email.


set -e

. ./ci/envoy_build_sha.sh

[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build
mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"

TEST_TYPE="bazel.coverity"
COVERITY_USER_EMAIL="${COVERITY_USER_EMAIL:-$(git config user.email)}"
COVERITY_OUTPUT_FILE="${ENVOY_DOCKER_BUILD_DIR}"/envoy/source/exe/envoy-coverity-output.tgz

if [ -n "${COVERITY_TOKEN}" ]
then
  pushd "${ENVOY_DOCKER_BUILD_DIR}"
  rm -rf cov-analysis
  wget https://scan.coverity.com/download/linux64 --post-data "token=${COVERITY_TOKEN}&project=Envoy+Proxy" -O coverity_tool.tgz
  tar xvf coverity_tool.tgz
  mv cov-analysis-linux* cov-analysis
  popd
else
  echo "ERROR: COVERITY_TOKEN is required to download and run Coverity Scan."
  exit 1
fi

ci/run_envoy_docker.sh "ci/do_ci.sh ${TEST_TYPE}"

# Check the artifact size as an approximation for determining if the scan tool was successful.
if [[ $(find "${COVERITY_OUTPUT_FILE}" -type f -size +256M 2>/dev/null) ]]
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
