licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "stats_interface",
    hdrs = ["stats.h"],
)

envoy_cc_library(
    name = "timespan",
    hdrs = ["timespan.h"],
    deps = [
        ":stats_interface",
        "//include/envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "stats_macros",
    hdrs = ["stats_macros.h"],
    deps = [":stats_interface"],
)
