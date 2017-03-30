package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "ratelimit_includes",
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/tracing:context_includes",
    ],
)
