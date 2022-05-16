load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "inputs_lib",
    hdrs = ["inputs.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "@envoy_api//envoy/extensions/matching/common_inputs/ssl/v3:pkg_cc_proto",
    ],
)
