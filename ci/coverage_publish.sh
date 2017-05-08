#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
set -e

if [ "${TRAVIS}" != "true" ]; then 
  exit 0
fi

# available for master builds and PRs from originating repository (not forks)
if [ "${TRAVIS_SECURE_ENV_VARS}" == "true" ]; then
  mkdir -p ~/.aws
  echo "[profile coverage]" >> ~/.aws/config
  echo "aws_access_key_id=${COVERAGE_AWS_ACCESS_KEY_ID}" >> ~/.aws/config
  echo "aws_secret_access_key=${COVERAGE_AWS_SECRET_ACCESS_KEY}" >> ~/.aws/config
  echo "region=us-east-1" >> ~/.aws/config

  COVERAGE_DIR="$(readlink -f ${ENVOY_BUILD_DIR}/bazel-envoy)/../envoy/generated/coverage"
  S3_LOCATION="lyft-envoy/coverage/report-${TRAVIS_BRANCH}"

  aws s3 cp "${COVERAGE_DIR}" "s3://${S3_LOCATION}" --recursive --profile coverage --acl public-read
  echo "Coverage report for this branch is available for viewing at https://s3.amazonaws.com/${S3_LOCATION}/coverage.html."
else
  echo "Coverage report will not be uploaded for this build."
fi
