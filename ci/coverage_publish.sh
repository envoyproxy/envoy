#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CircleCI logs.
set -e

[[ -z "${ENVOY_BUILD_DIR}" ]] && ENVOY_BUILD_DIR=/build/envoy
COVERAGE_FILE="${ENVOY_BUILD_DIR}/generated/coverage/index.html"

if [ ! -f "${COVERAGE_FILE}" ]; then
  echo "ERROR: Coverage file not found."
  exit 1
fi

# available for master builds
if [[ "${BUILD_REASON}" != "PullRequest" ]]; then
  echo "Uploading coverage report..."

  BRANCH_NAME="${BUILD_SOURCEBRANCH/refs\/heads\//}"
  COVERAGE_DIR="$(dirname "${COVERAGE_FILE}")"
  GCS_LOCATION="envoy-coverage/report-${BRANCH_NAME}"

  echo ${GCP_SERVICE_ACCOUNT_KEY} | base64 --decode | gcloud auth activate-service-account --key-file=-
  gsutil -m rsync -dr ${COVERAGE_DIR} gs://${GCS_LOCATION}
  echo "Coverage report for branch '${BRANCH_NAME}': https://storage.googleapis.com/${GCS_LOCATION}/index.html"
else
  echo "Coverage report will not be uploaded for this build."
fi

pip3 install -I https://github.com/lizan/lcov-to-cobertura-xml/archive/e4c27b8dc3e63cbb6d39f7bedfe2e1e6bec625f2.tar.gz

COVERAGE_DATA="${ENVOY_BUILD_DIR}/generated/coverage/coverage.dat"
COVERAGE_XML="${ENVOY_BUILD_DIR}/coverage.xml"

python3 -m lcov_cobertura -b ${PWD} ${COVERAGE_DATA} -o ${COVERAGE_XML}
