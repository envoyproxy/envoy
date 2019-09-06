#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

if [ "${CIRCLECI}" != "true" ]; then
  exit 0
fi

[[ -z "${ENVOY_BUILD_DIR}" ]] && ENVOY_BUILD_DIR=/build
COVERAGE_FILE="${ENVOY_BUILD_DIR}/envoy/generated/coverage/index.html"

if [ ! -f "${COVERAGE_FILE}" ]; then
  echo "ERROR: Coverage file not found."
  exit 1
fi

# available for master builds
if [ -z "$CIRCLE_PR_NUMBER" ]
then
  echo "Uploading coverage report..."

  BRANCH_NAME="${CIRCLE_BRANCH}"
  COVERAGE_DIR="$(dirname "${COVERAGE_FILE}")"
  GCS_LOCATION="envoy-coverage/report-${BRANCH_NAME}"

  echo ${GCP_SERVICE_ACCOUNT_KEY} | base64 --decode | gcloud auth activate-service-account --key-file=-
  gsutil -m rsync -dr ${COVERAGE_DIR} gs://${GCS_LOCATION}
  echo "Coverage report for branch '${BRANCH_NAME}': https://storage.googleapis.com/${GCS_LOCATION}/index.html"
else
  echo "Coverage report will not be uploaded for this build."
fi
