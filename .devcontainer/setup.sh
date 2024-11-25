#!/usr/bin/env bash

BAZELRC_FILE=~/.bazelrc bazel/setup_clang.sh /opt/llvm

if [[ -n "${BAZEL_BUILD_EXTRA_OPTIONS}" ]]; then
    echo "build ${BAZEL_BUILD_EXTRA_OPTIONS}" | tee -a ~/.bazelrc
fi

# Ideally we want this line so bazel doesn't pollute things outside of the devcontainer, but some of
# API tooling (proto_sync) depends on symlink like bazel-bin.
# TODO(lizan): Fix API tooling and enable this again
#echo "build --symlink_prefix=/" >> ~/.bazelrc

[[ -n "${BUILD_DIR}" ]] && sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"
