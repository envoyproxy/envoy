load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Redis custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "http_inputs",
    hdrs = ["inputs.h"],
    deps = [
        "//source/common/http:matching_data_lib",
        "//source/common/matcher:matcher_lib",
    ],
)

envoy_cc_library(
    name = "config",
    hdrs = ["config.h"],
    srcs = ["config.cc"],
    deps = [
        ":http_inputs",
        "//source/common/http:matching_data_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/extensions/matching/input/v3:pkg_cc_proto",
        ],
    #security_posture = "unknown",
)