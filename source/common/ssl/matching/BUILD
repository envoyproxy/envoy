load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "inputs_lib",
    srcs = ["inputs.cc"],
    hdrs = ["inputs.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "@envoy_api//envoy/extensions/matching/common_inputs/ssl/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)
