#!/bin/bash

set -e

[ -z "${ENVOY_SRC_DIR}" ] && { echo "ENVOY_SRC_DIR not set"; exit 1; }

PREBUILT_DIR="${ENVOY_SRC_DIR}"/build/prebuilt
mkdir -p "${PREBUILT_DIR}"
PREBUILT_DIR_REAL=$(realpath "${PREBUILT_DIR}")

export THIRDPARTY_DEPS="${PREBUILT_DIR_REAL}"
export THIRDPARTY_SRC="${PREBUILT_DIR_REAL}/thirdparty"
export THIRDPARTY_BUILD="${PREBUILT_DIR_REAL}/thirdparty_build"
export BUILD_DISTINCT=1

"${ENVOY_SRC_DIR}"/ci/build_container/build_and_install_deps.sh "${PREBUILT_DIR_REAL}"
