#!/bin/bash

set -e

if [[ ! -z "${BAZEL_REMOTE_CACHE}" ]]; then
  if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
    # mktemp will create a tempfile with u+rw permission minus umask, it will not be readable by all
    # users by default.
    GCP_SERVICE_ACCOUNT_KEY_FILE=$(mktemp -t gcp_service_account.XXXXXX.json)

    gcp_service_account_cleanup() {
      echo "Deleting service account key file..."
      rm -rf "${GCP_SERVICE_ACCOUNT_KEY_FILE}"
    }

    trap gcp_service_account_cleanup EXIT

    echo "${GCP_SERVICE_ACCOUNT_KEY}" | base64 --decode > "${GCP_SERVICE_ACCOUNT_KEY_FILE}"

    export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
      --remote_http_cache=${BAZEL_REMOTE_CACHE} \
      --google_credentials=${GCP_SERVICE_ACCOUNT_KEY_FILE}"
    echo "Set up bazel read/write HTTP cache at ${BAZEL_REMOTE_CACHE}."
  else
    export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
      --remote_http_cache=${BAZEL_REMOTE_CACHE} --noremote_upload_local_results"
    echo "Set up bazel read only HTTP cache at ${BAZEL_REMOTE_CACHE}."
  fi
else
  echo "No remote cache bucket is set, skipping setup remote cache."
fi
