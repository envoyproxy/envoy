"""Build file for Envoy Mobile bzlmod extensions."""

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")

package(default_visibility = ["//visibility:public"])

# This directory contains bzlmod extensions for Envoy Mobile repository setup.
# Extensions handle mobile-specific dependencies, toolchains, and workspace configurations.

bzl_library(
    name = "extensions",
    srcs = glob(["*.bzl"]),
    visibility = ["//visibility:public"],
)
