#!/bin/bash

set -e

$(dirname "$0")/activate_gcp_key.sh

[[ ! -z "$(gcloud config get-value account 2> /dev/null)" ]] && export BAZEL_BUILD_EXTRA_OPTIONS+=" --google_default_credentials"

if [[ ! -z "${BAZEL_REMOTE_CACHE}" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_cache=${BAZEL_REMOTE_CACHE}"
  echo "Set up bazel remote read/write cache at ${BAZEL_REMOTE_CACHE}."

  if [[ ! -z "${BAZEL_REMOTE_INSTANCE}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --remote_instance_name=${BAZEL_REMOTE_INSTANCE}"
    echo "instance_name: ${BAZEL_REMOTE_INSTANCE}."
  elif [[ -z "${ENVOY_RBE}" ]]; then
    export BAZEL_BUILD_EXTRA_OPTIONS+=" --jobs=HOST_CPUS*.9 --remote_timeout=600"
    echo "using local build cache."
  fi

else
  echo "No remote cache is set, skipping setup remote cache."
fi
