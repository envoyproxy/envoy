#!/bin/bash

set -e

# When debugging the build, it's helpful to keep the artifacts in /tmp, since they don't get
# repeatedly clobbered as build recipes are modified. This is controlled by debug_build in
# bazel/respositories.bzl.
if [[ "${DEBUG}" == "1" ]]
then
  BASEDIR=/tmp/bazel-envoy-deps
else
  BASEDIR="${PWD}/build"
fi

export THIRDPARTY_DEPS="${BASEDIR}"
export THIRDPARTY_SRC="${BASEDIR}/thirdparty"
export THIRDPARTY_BUILD="${BASEDIR}/thirdparty_build"

time ./build_and_install_deps.sh "${PREBUILT_DIR_REAL}"

ln -sf "$(realpath "${THIRDPARTY_SRC}")" thirdparty
ln -sf "$(realpath "${THIRDPARTY_BUILD}")" thirdparty_build
