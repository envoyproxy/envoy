#!/bin/bash

set -e

CI_TARGET=$1
shift

if [[ "$CI_TARGET" == "bazel.gcc" ]]; then
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-gcc --linkopt=-fuse-ld=gold"
  CI_TARGET="bazel.release"
elif [[ "$CI_TARGET" == "bazel.compile_time_options" ]]; then
  # TODO(lizan): combine this with ci/do_ci.sh target
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-libc++ \
    --define signal_trace=disabled \
    --define hot_restart=disabled \
    --define google_grpc=disabled \
    --define boringssl=fips \
    --define log_debug_assert_in_release=enabled \
    --define quiche=enabled \
    --define path_normalization_by_default=true \
  "
  CI_TARGET="bazel.release"
else
  export BAZEL_BUILD_EXTRA_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS} --config=remote-clang"
fi

exec ci/do_ci.sh "${CI_TARGET}" $*
