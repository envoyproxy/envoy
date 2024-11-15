#!/usr/bin/env bash

set -e -o pipefail

GCS_ARTIFACT_BUCKET="${1:-}"
GCP_SERVICE_ACCOUNT_KEY_PATH="${2:-}"
UPLOAD_DIRECTORY="${3:-}"
TARGET_SUFFIX="${4:-}"
REDIRECT_PATH="${5:-}"

#
# $ bazel run //tools/gcs:upload BUCKETNAME KEY_PATH REDIRECT_PATH


if [[ -z "${GSUTIL}" ]]; then
    echo "GSUTIL is not set, not uploading artifacts."
    exit 1
fi

if [[ -z "${ENVOY_SOURCE_DIR}" ]]; then
    echo "ENVOY_SOURCE_DIR is not set, not uploading artifacts."
    exit 1
fi

if [[ -z "${GCS_ARTIFACT_BUCKET}" ]]; then
    echo "Artifact bucket is not set, not uploading artifacts."
    exit 1
fi

if [[ ! -s "${GCP_SERVICE_ACCOUNT_KEY_PATH}" ]]; then
    echo "GCP key path is not set, not uploading artifacts."
    exit 1
fi

if [[ -z "${UPLOAD_DIRECTORY}" ]]; then
    echo "UPLOAD_DIRECTORY is not set, not uploading artifacts."
    exit 1
fi

if [[ -z "${TARGET_SUFFIX}" ]]; then
    echo "TARGET_SUFFIX is not set, not uploading artifacts."
    exit 1
fi

cat <<EOF > ~/.boto
[Credentials]
gs_service_key_file=${GCP_SERVICE_ACCOUNT_KEY_PATH}
EOF


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

UPLOAD_PATH="$(git -C "${ENVOY_SOURCE_DIR}" rev-parse HEAD | head -c7)"
GCS_LOCATION="${GCS_ARTIFACT_BUCKET}/${UPLOAD_PATH}/${TARGET_SUFFIX}"

echo "Uploading to gs://${GCS_LOCATION} ..."
"${GSUTIL}" \
         -mq rsync \
         -dr "${UPLOAD_DIRECTORY}" \
         "gs://${GCS_LOCATION}"

if [[ -z "${REDIRECT_PATH}" ]]; then
    echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_LOCATION}" >&2
    exit 0
fi

TMP_REDIRECT="/tmp/redirect/${REDIRECT_PATH}/${TARGET_SUFFIX}"
mkdir -p "$TMP_REDIRECT"
echo "<meta http-equiv=\"refresh\" content=\"0; URL='https://storage.googleapis.com/${GCS_LOCATION}/index.html'\" />" \
     >  "${TMP_REDIRECT}/index.html"
GCS_REDIRECT="${GCS_ARTIFACT_BUCKET}/${REDIRECT_PATH}/${TARGET_SUFFIX}"
echo "Uploading redirect to gs://${GCS_REDIRECT} ..."  >&2
"${GSUTIL}" \
         -h "Cache-Control:no-cache,max-age=0" \
         -mq rsync \
         -dr "${TMP_REDIRECT}" \
         "gs://${GCS_REDIRECT}"

echo "Artifacts uploaded to: https://storage.googleapis.com/${GCS_REDIRECT}/index.html" >&2
echo "https://storage.googleapis.com/${GCS_REDIRECT}/index.html"
