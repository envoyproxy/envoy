#!/bin/bash

set -e -o pipefail

if [[ -z "${GCS_ARTIFACT_BUCKET}" ]]; then
  echo "Artifact bucket is not set, not uploading artifacts."
  exit 0
fi

# Fail when service account key is not specified
bash -c 'echo ${GCP_SERVICE_ACCOUNT_KEY}' | base64 --decode | gcloud auth activate-service-account --key-file=-

SOURCE_DIRECTORY="$1"
TARGET_SUFFIX="$2"

if [ ! -d "${SOURCE_DIRECTORY}" ]; then
  echo "ERROR: ${SOURCE_DIRECTORY} is not found."
  exit 1
fi

BRANCH=${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}
GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${BRANCH}/${TARGET_SUFFIX}"

echo "Uploading to gs://${GCS_LOCATION} ..."
gsutil -mq rsync -dr ${SOURCE_DIRECTORY} gs://${GCS_LOCATION}
echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_LOCATION}/index.html"
