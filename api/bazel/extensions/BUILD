"""Build file for Envoy API bzlmod extensions."""

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")

package(default_visibility = ["//visibility:public"])

# This directory contains bzlmod extensions for Envoy API repository setup.
# Extensions handle API-specific dependencies and configurations.

bzl_library(
    name = "extensions",
    srcs = glob(["*.bzl"]),
    visibility = ["//visibility:public"],
)
