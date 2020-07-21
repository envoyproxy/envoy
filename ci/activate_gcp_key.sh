#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e -o pipefail

if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
  echo ${GCP_SERVICE_ACCOUNT_KEY} | base64 --decode | gcloud auth activate-service-account --key-file=-
fi
