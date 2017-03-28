package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "stats_includes",
    hdrs = ["stats.h"],
)

envoy_cc_library(
    name = "stats_macros",
    hdrs = ["stats_macros.h"],
    deps = [":stats_includes"],
)
