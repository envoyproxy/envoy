#!/bin/bash

# Run this file when adding a new or upgrading an existing external dependency.

set -e

[ -z "${ENVOY_SRC_DIR}" ] && { echo "ENVOY_SRC_DIR not set"; exit 1; }

"${ENVOY_SRC_DIR}"/tools/setup_external_deps.sh
"${ENVOY_SRC_DIR}"/bazel/gen_prebuilt.py "${ENVOY_SRC_DIR}"/build/prebuilt \
  "${ENVOY_SRC_DIR}"/bazel/prebuilt.bzl
