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

  bash -c 'echo "${GCP_SERVICE_ACCOUNT_KEY}"' | base64 --decode > "${GCP_SERVICE_ACCOUNT_KEY_FILE}"

  export BAZEL_BUILD_EXTRA_OPTIONS+=" --google_credentials=${GCP_SERVICE_ACCOUNT_KEY_FILE}"
fi


if [[ -n "${BAZEL_REMOTE_CACHE}" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_cache=${BAZEL_REMOTE_CACHE}"
  echo "Set up bazel remote read/write cache at ${BAZEL_REMOTE_CACHE}."

  if [[ -n "${BAZEL_REMOTE_INSTANCE}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_instance_name=${BAZEL_REMOTE_INSTANCE}"
    echo "instance_name: ${BAZEL_REMOTE_INSTANCE}."
  elif [[ -z "${ENVOY_RBE}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --jobs=HOST_CPUS*.9 --remote_timeout=600"
    echo "using local build cache."
  fi

else
  echo "No remote cache is set, skipping setup remote cache."
fi
