#!/bin/bash

set -e

export RBE_AUTOCONF_ROOT=$(bazel info workspace)

rm -rf "${RBE_AUTOCONF_ROOT}"/bazel/toolchains/configs/*
cp -vf "${RBE_AUTOCONF_ROOT}/bazel/toolchains/empty.bzl" "${RBE_AUTOCONF_ROOT}/bazel/toolchains/configs/versions.bzl"

# Bazel query is the right command so bazel won't fail itself.
bazel query ${BAZEL_QUERY_OPTIONS} "@rbe_ubuntu_clang_gen//..."
bazel query ${BAZEL_QUERY_OPTIONS} "@rbe_ubuntu_clang_libcxx_gen//..."
bazel query ${BAZEL_QUERY_OPTIONS} "@rbe_ubuntu_gcc_gen//..."
