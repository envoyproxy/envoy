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

  if [[ -n "${GOOGLE_BES_PROJECT_ID}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --config=rbe-google-bes --bes_instance_name=${GOOGLE_BES_PROJECT_ID}"
  fi

fi

if [[ -n "${BAZEL_REMOTE_CACHE}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_cache=${BAZEL_REMOTE_CACHE}"
    echo "Set up bazel remote read/write cache at ${BAZEL_REMOTE_CACHE}."

    if [[ -z "${ENVOY_RBE}" ]]; then
        export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_timeout=600"
        echo "using local build cache."
        # Normalize branches - `release/vX.xx`, `vX.xx`, `vX.xx.x` -> `vX.xx`
        TARGET_BRANCH="${CI_TARGET_BRANCH}"
        if [[ "$TARGET_BRANCH" =~ ^origin/ ]]; then
            TARGET_BRANCH=$(echo "$TARGET_BRANCH" | cut -d/ -f2-)
        fi
        BRANCH_NAME="$(echo "${TARGET_BRANCH}" | cut -d/ -f2 | cut -d. -f-2)"
        if [[ "$BRANCH_NAME" == "merge" ]]; then
            # Manually run PR commit - there is no easy way of telling which branch
            # it is, so just set it to `main` - otherwise it tries to cache as `branch/merge`
            BRANCH_NAME=main
        fi
        BAZEL_REMOTE_INSTANCE="branch/${BRANCH_NAME}"
    fi

    if [[ -n "${BAZEL_REMOTE_INSTANCE}" ]]; then
        export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_instance_name=${BAZEL_REMOTE_INSTANCE}"
        echo "instance_name: ${BAZEL_REMOTE_INSTANCE}."
    fi
fi
