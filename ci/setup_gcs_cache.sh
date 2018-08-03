#!/bin/bash

set -e

gcp_service_account_cleanup() {
  echo "Deleting service account key file..."
  rm -rf /tmp/gcp_service_account.json
}

if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" && ! -z "${BAZEL_REMOTE_CACHE}" ]]; then
  echo "${GCP_SERVICE_ACCOUNT_KEY}" | base64 --decode > /tmp/gcp_service_account.json
  trap gcp_service_account_cleanup EXIT

  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
    --remote_http_cache=${BAZEL_REMOTE_CACHE} --google_credentials=/tmp/gcp_service_account.json"
  echo "Set up bazel HTTP cache at ${BAZEL_REMOTE_CACHE}."
else
  echo "No GCP service account key or remote cache bucket is set, skipping setup remote cache."
fi
