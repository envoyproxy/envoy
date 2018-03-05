licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_basic_cc_library",
    "envoy_package",
)

envoy_package()

envoy_basic_cc_library(
    name = "base_includes",
    hdrs = [
        "exception.h",
        "pure.h",
    ],
    include_prefix = "envoy/common",
)

envoy_cc_library(
    name = "time_interface",
    hdrs = ["time.h"],
)

envoy_cc_library(
    name = "interval_set_interface",
    hdrs = ["interval_set.h"],
)

envoy_cc_library(
    name = "callback",
    hdrs = ["callback.h"],
)
