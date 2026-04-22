#!/usr/bin/env bash

echo "build --config=clang" >> user.bazelrc

# Ideally we want this line so bazel doesn't pollute things outside of the devcontainer, but some of
# API tooling (proto_sync) depends on symlink like bazel-bin.
# TODO(lizan): Fix API tooling and enable this again
#echo "build --symlink_prefix=/" >> ~/.bazelrc

echo "BUILD_DIR=${BUILD_DIR} ENVOY_SRCDIR=${ENVOY_SRCDIR}"
sudo chown -R "$(id -u):$(id -g)" "${BUILD_DIR}"

# Ensure that toolchain is downloaded.
./ci/do_ci.sh pre_refresh_compdb

sudo ln -sf "${BUILD_DIR}"/bazel_root/base-envoy-compdb/external/llvm_toolchain_llvm/bin/{clang-format,clangd} /usr/local/bin/
