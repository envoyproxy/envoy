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

if [[ -n "${SHORT_COMMIT_SHA}" ]]; then
    UPLOAD_PATH="${SHORT_COMMIT_SHA}"
    LINKED_PATH="${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}"
else
    UPLOAD_PATH="${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}"
fi

GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${UPLOAD_PATH}/${TARGET_SUFFIX}"

echo "Uploading to gs://${GCS_LOCATION} ..."
gsutil -mq rsync -dr "${SOURCE_DIRECTORY}" "gs://${GCS_LOCATION}"

if [[ -n "$LINKED_PATH" ]]; then
    TMPLINK="/tmp/docredirect/${LINKED_PATH}/docs"
    mkdir -p "$TMPLINK"
    echo "<meta http-equiv=\"refresh\" content=\"0; URL='https://storage.googleapis.com/${GCS_LOCATION}/index.html'\" />" \
	 >  "${TMPLINK}/index.html"
    GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${LINKED_PATH}"
    echo "Uploading redirect to gs://${GCS_LOCATION} ..."
    gsutil -mq rsync -dr "/tmp/docredirect/${LINKED_PATH}" "gs://${GCS_LOCATION}"
fi

echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_LOCATION}/index.html"
