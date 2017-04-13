#!/bin/bash

set -e

# When debugging the build, it's helpful to keep the artifacts in /tmp, since they don't get
# repeatedly clobbered as build recipes are modified. This is controlled by debug_build in
# bazel/respositories.bzl.
if [[ "${DEBUG}" == "1" ]]
then
  BASEDIR=/tmp/bazel-envoy-deps
  # Tell build_and_install_deps.sh to build sequentially when performance debugging.
  # export BUILD_CONCURRENCY=0
else
  BASEDIR="${PWD}/build"
fi

mkdir -p  "${BASEDIR}"

export THIRDPARTY_DEPS="${BASEDIR}"
export THIRDPARTY_SRC="${BASEDIR}/thirdparty"
export THIRDPARTY_BUILD="${BASEDIR}/thirdparty_build"

DEPS=""
for r in "$@"
do
  DEPS="${DEPS} ${THIRDPARTY_DEPS}/$r.dep"
done

set -o pipefail
(time ./build_and_install_deps.sh ${DEPS}) 2>&1 | \
  tee "${BASEDIR}"/build.log

# Need to rsync in debug mode, since the symlinks are into /tmp and cause problems with later build
# sandboxing.
if [[ "${DEBUG}" == "1" ]]
then
  rsync -a "$(realpath "${THIRDPARTY_SRC}")"/ thirdparty
  rsync -a "$(realpath "${THIRDPARTY_BUILD}")"/ thirdparty_build
else
  ln -sf "$(realpath "${THIRDPARTY_SRC}")" thirdparty
  ln -sf "$(realpath "${THIRDPARTY_BUILD}")" thirdparty_build
fi
