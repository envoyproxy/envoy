licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "attribute_context_lib",
    hdrs = ["attribute_context.h"],
    srcs = ["attribute_context.cc"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//source/common/http:utility_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/service/auth/v2alpha:attribute_context_cc",
    ],
)
