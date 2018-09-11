#!/bin/bash

set -e

function dump_cc_version {
    local XCC="${CC:-gcc}"

    if [[ $(uname -s) == "Linux" ]]; then
        local XCC_PATH=$(readlink -f $(which "${XCC}"))
        local XCC_PKG=$(dpkg -S "${XCC}" | grep "${XCC_PATH}" | cut -d':' -f1)
        local XCC_VERSION=$(dpkg-query --showformat='${Version}' --show "${XCC_PKG}")
    else
        local XCC_VERSION=$(clang -v 2>&1 | head -n 1)
    fi
    echo "Setting up $1 remote cache for ${XCC_VERSION}"
}

if [[ ! -z "${BAZEL_REMOTE_CACHE}" ]]; then
  if [[ ! -z "${GCP_SERVICE_ACCOUNT_KEY}" ]]; then
    dump_cc_version "read/write"
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
    dump_cc_version "read-only"
    export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} \
      --remote_http_cache=${BAZEL_REMOTE_CACHE} --noremote_upload_local_results"
    echo "Set up bazel read only HTTP cache at ${BAZEL_REMOTE_CACHE}."
  fi
else
  echo "No remote cache bucket is set, skipping setup remote cache."
fi
