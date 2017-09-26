#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# Travis logs.
set -e

if [ "${CIRCLECI}" != "true" ]; then
  exit 0
fi

# available for master builds and PRs from originating repository (not forks)
if [ -z "$CIRCLE_PULL_REQUEST" ]
then
  echo "Uploading coverage report..."
  
  [[ -z "${ENVOY_BUILD_DIR}" ]] && ENVOY_BUILD_DIR=/build
  COVERAGE_FILE="${ENVOY_BUILD_DIR}/envoy/generated/coverage/coverage.html"

  if [ ! -f "${COVERAGE_FILE}" ]; then
    echo "ERROR: Coverage file not found."
    exit 1
  fi

  BRANCH_NAME="${CIRCLE_BRANCH}"
  COVERAGE_DIR="$(dirname "${COVERAGE_FILE}")"
  S3_LOCATION="lyft-envoy/coverage/report-${BRANCH_NAME}"

  pip install awscli --upgrade
  aws s3 cp "${COVERAGE_DIR}" "s3://${S3_LOCATION}" --recursive --acl public-read --quiet --sse
  echo "Coverage report for branch '${BRANCH_NAME}': https://s3.amazonaws.com/${S3_LOCATION}/coverage.html"
else
  echo "Coverage report will not be uploaded for this build."
fi
