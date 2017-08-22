#!/bin/bash

set -e

if [[ `uname` == "Darwin" ]]
then
  function md5sum {
    md5
  }
fi

# Tell build_and_install_deps.sh to build sequentially when performance debugging.
# export BUILD_CONCURRENCY=0

# Hash environment variables we care about to force rebuilds when they change.
ENV_HASH=$(echo "${CC} ${CXX} ${LD_LIBRARY_PATH}" | md5sum | cut -f 1 -d\ )

# Don't build inside the directory Bazel believes the repository_rule output goes. Instead, do so in
# a parallel directory. This allows the build artifacts to survive Bazel clobbering the repostory
# directory when a small change to repositories.bzl or a build recipe happens. We then rely on make
# dependency analysis to detect when stuff needs to be rebuilt.
BASEDIR="${PWD}_cache_${ENV_HASH}"

>&2 echo "External dependency cache directory ${BASEDIR}"
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
BUILD_LOG="${BASEDIR}"/build.log
(time ./build_and_install_deps.sh ${DEPS}) 2>&1 | tee "${BUILD_LOG}" >&2

ln -sf "$(realpath "${THIRDPARTY_SRC}")" thirdparty
ln -sf "$(realpath "${THIRDPARTY_BUILD}")" thirdparty_build
