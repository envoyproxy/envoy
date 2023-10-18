#!/bin/bash

set -e

if [[ -n "${GCP_SERVICE_ACCOUNT_KEY:0:1}" ]]; then
  # mktemp will create a tempfile with u+rw permission minus umask, it will not be readable by all
  # users by default.
  GCP_SERVICE_ACCOUNT_KEY_FILE=$(mktemp -t gcp_service_account.XXXXXX.json)

  gcp_service_account_cleanup() {
    echo "Deleting service account key file..."
    rm -rf "${GCP_SERVICE_ACCOUNT_KEY_FILE}"
  }

  trap gcp_service_account_cleanup EXIT

  echo "Setting GCP_SERVICE_ACCOUNT_KEY is deprecated, please place your decoded GCP key in " \
       "an exported/shared tmp directory and add it to BAZEL_BUILD_EXTRA_OPTIONS, eg: " >&2
  # shellcheck disable=SC2086
  echo "$ export ENVOY_SHARED_TMP_DIR=/tmp/envoy-shared" \
       "$ ENVOY_RBE_KEY_PATH=$(mktemp -p \"${ENVOY_SHARED_TMP_DIR}\" -t gcp_service_account.XXXXXX.json)" \
       "$ bash -c 'echo \"$(GcpServiceAccountKey)\"' | base64 --decode > \"${ENVOY_RBE_KEY_PATH}\"" \
       "$ export BAZEL_BUILD_EXTRA_OPTIONS+=\" --google_credentials=${ENVOY_RBE_KEY_PATH}\"" >&2
  bash -c 'echo "${GCP_SERVICE_ACCOUNT_KEY}"' | base64 --decode > "${GCP_SERVICE_ACCOUNT_KEY_FILE}"
  export BAZEL_BUILD_EXTRA_OPTIONS+=" --google_credentials=${GCP_SERVICE_ACCOUNT_KEY_FILE}"
fi

if [[ -n "${BAZEL_REMOTE_CACHE}" ]]; then
    echo "Setting BAZEL_REMOTE_CACHE is deprecated, please use BAZEL_BUILD_EXTRA_OPTIONS " \
         "or use a user.bazelrc config " >&2
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_cache=${BAZEL_REMOTE_CACHE}"
    echo "Set up bazel remote read/write cache at ${BAZEL_REMOTE_CACHE}."
    if [[ -n "${BAZEL_REMOTE_INSTANCE}" ]]; then
        export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_instance_name=${BAZEL_REMOTE_INSTANCE}"
        echo "instance_name: ${BAZEL_REMOTE_INSTANCE}."
    fi
fi
