#!/usr/bin/env bash

. ci/setup_cache.sh
trap - EXIT

BAZELRC_FILE=~/.bazelrc bazel/setup_clang.sh /opt/llvm

# Use generated toolchain config because we know the base container is the one we're using in RBE.
# Not using libc++ here because clangd will raise some tidy issue in libc++ header as of version 9.
echo "build --config=rbe-toolchain-clang" >> ~/.bazelrc
echo "build --symlink_prefix=/" >> ~/.bazelrc
echo "build ${BAZEL_BUILD_EXTRA_OPTIONS}" | tee -a ~/.bazelrc
