load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

# Deps can be inferred, irrelevant deps are removed.
envoy_cc_library(
    name = "foo",
    srcs = ["canonical_api_deps.cc", "canonical_api_deps.other.cc"],
    hdrs = ["canonical_api_deps.h"],
    deps = ["@envoy_api//envoy/types:pkg_cc_proto"],
)
