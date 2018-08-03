#!/bin/bash

set -e

gcp_service_account_cleanup() {
  echo "Deleting service account key file..."
  rm -rf /tmp/gcp_service_account.json
}

if [[ ! -z "${BAZEL_REMOTE_CACHE}" ]]; then

  if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
    echo "${GCP_SERVICE_ACCOUNT_KEY}" | base64 --decode > /tmp/gcp_service_account.json
    trap gcp_service_account_cleanup EXIT

    export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
      --remote_http_cache=${BAZEL_REMOTE_CACHE} --google_credentials=/tmp/gcp_service_account.json"
    echo "Set up bazel read/write HTTP cache at ${BAZEL_REMOTE_CACHE}."
  else
    export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
      --remote_http_cache=${BAZEL_REMOTE_CACHE} --noremote_upload_local_results"
    echo "Set up bazel read only HTTP cache at ${BAZEL_REMOTE_CACHE}."
  fi

else
  echo "No remote cache bucket is set, skipping setup remote cache."
fi
