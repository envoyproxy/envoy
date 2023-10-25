#!/bin/bash

set -e -o pipefail

if [[ -z "${GCS_ARTIFACT_BUCKET}" ]]; then
    echo "Artifact bucket is not set, not uploading artifacts."
    exit 1
fi

read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTION_LIST:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"

if [[ ! -s "${GCP_SERVICE_ACCOUNT_KEY_PATH}" ]]; then
    echo "GCP key is not set, not uploading artifacts."
    exit 1
fi

cat <<EOF > ~/.boto
[Credentials]
gs_service_key_file=${GCP_SERVICE_ACCOUNT_KEY_PATH}
EOF

SOURCE_DIRECTORY="$1"
TARGET_SUFFIX="$2"

if [ ! -d "${SOURCE_DIRECTORY}" ]; then
    echo "ERROR: ${SOURCE_DIRECTORY} is not found."
    exit 1
fi

# Upload to the last commit sha (first 7 chars)
# the bucket is either `envoy-postsubmit` or `envoy-pr`
#
# For example, docs might be uploaded to:
#
#   https://storage.googleapis.com/envoy-pr/20b4893/docs/index.html
#
# With a redirect from the PR to the latest build:
#
#   https://storage.googleapis.com/envoy-pr/28462/docs/index.html
#

UPLOAD_PATH="$(git rev-parse HEAD | head -c7)"
REDIRECT_PATH="${SYSTEM_PULLREQUEST_PULLREQUESTNUMBER:-${BUILD_SOURCEBRANCHNAME}}"
GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${UPLOAD_PATH}/${TARGET_SUFFIX}"

echo "Uploading to gs://${GCS_LOCATION} ..."
bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" \
      //tools/gsutil \
      -- -mq rsync \
         -dr "${SOURCE_DIRECTORY}" \
         "gs://${GCS_LOCATION}"

TMP_REDIRECT="/tmp/redirect/${REDIRECT_PATH}/${TARGET_SUFFIX}"
mkdir -p "$TMP_REDIRECT"
echo "<meta http-equiv=\"refresh\" content=\"0; URL='https://storage.googleapis.com/${GCS_LOCATION}/index.html'\" />" \
     >  "${TMP_REDIRECT}/index.html"
GCS_REDIRECT="${GCS_ARTIFACT_BUCKET}/${REDIRECT_PATH}/${TARGET_SUFFIX}"
echo "Uploading redirect to gs://${GCS_REDIRECT} ..."
bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" \
      //tools/gsutil \
      -- -h "Cache-Control:no-cache,max-age=0" \
         -mq rsync \
         -dr "${TMP_REDIRECT}" \
         "gs://${GCS_REDIRECT}"

if [[ "${COVERAGE_FAILED}" -eq 1 ]]; then
    echo "##vso[task.logissue type=error]Coverage failed, check artifact at: https://storage.googleapis.com/${GCS_LOCATION}/index.html"
fi

echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_LOCATION}/index.html"
