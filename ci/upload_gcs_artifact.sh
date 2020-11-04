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

if [[ "$TARGET_SUFFIX" == "docs" ]]; then
    # docs upload to the last commit sha (first 7 chars) in the developers branch
    UPLOAD_PATH="$(git log --pretty=%P -n 1 | cut -d' ' -f2 | head -c7)"
else
    UPLOAD_PATH="${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}"
fi

GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${UPLOAD_PATH}/${TARGET_SUFFIX}"

echo "Uploading to gs://${GCS_LOCATION} ..."
gsutil -mq rsync -dr "${SOURCE_DIRECTORY}" "gs://${GCS_LOCATION}"

# For docs uploads, add a redirect `PR_NUMBER` -> `COMMIT_SHA`
if [[ "$TARGET_SUFFIX" == "docs" ]]; then
    REDIRECT_PATH="${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}"
    TMP_REDIRECT="/tmp/docredirect/${REDIRECT_PATH}/docs"
    mkdir -p "$TMP_REDIRECT"
    echo "<meta http-equiv=\"refresh\" content=\"0; URL='https://storage.googleapis.com/${GCS_LOCATION}/index.html'\" />" \
	 >  "${TMP_REDIRECT}/index.html"
    GCS_REDIRECT="${GCS_ARTIFACT_BUCKET}/${REDIRECT_PATH}"
    echo "Uploading redirect to gs://${GCS_REDIRECT} ..."
    gsutil -h "Cache-Control:no-cache,max-age=0" -mq rsync -dr "/tmp/docredirect/${REDIRECT_PATH}" "gs://${GCS_REDIRECT}"
fi

echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_LOCATION}/index.html"
