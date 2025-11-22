"""Build file for Envoy bzlmod extensions."""

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")

package(default_visibility = ["//visibility:public"])

# This directory contains bzlmod extensions for Envoy's main module repository setup.
# Extensions handle core dependencies, API bindings, and toolchain imports.
#
# Extension naming follows simplified conventions:
#   - dependencies: Core dependency setup
#   - dependency_imports: Toolchain imports and registrations
#   - api_*: API-related extensions
#   - repo: Repository metadata setup
#
# Usage patterns:
#   Main module: use_extension("//bazel/extensions:NAME.bzl", "NAME")
#   Submodules: use_extension("@envoy//bazel/extensions:NAME.bzl", "NAME")

bzl_library(
    name = "extensions",
    srcs = glob(["*.bzl"]),
    visibility = ["//visibility:public"],
)
