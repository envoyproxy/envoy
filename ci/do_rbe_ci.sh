#!/bin/bash

set -e

CI_TARGET=$1
shift

export ENVOY_RBE=1

if [[ "$CI_TARGET" == "bazel.gcc" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-gcc"
  CI_TARGET="bazel.release"
elif [[ "$CI_TARGET" == "bazel.compile_time_options" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-libc++"
else
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-clang"
fi

exec ci/do_ci.sh "${CI_TARGET}" $*
