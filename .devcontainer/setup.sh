#!/usr/bin/env bash

BAZELRC_FILE=~/.bazelrc bazel/setup_clang.sh /opt/llvm

# TODO(phlax): use user.bazelrc
# Use generated toolchain config because we know the base container is the one we're using in RBE.
# Not using libc++ here because clangd will raise some tidy issue in libc++ header as of version 9.
echo "build --config=rbe-toolchain-clang-libc++" >> ~/.bazelrc
echo "build ${BAZEL_BUILD_EXTRA_OPTIONS}" | tee -a ~/.bazelrc

# Ideally we want this line so bazel doesn't pollute things outside of the devcontainer, but some of
# API tooling (proto_sync) depends on symlink like bazel-bin.
# TODO(lizan): Fix API tooling and enable this again
#echo "build --symlink_prefix=/" >> ~/.bazelrc

[[ -n "${BUILD_DIR}" ]] && sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"
