licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_info_interface",
    hdrs = ["request_info.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/common:time_interface",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:upstream_interface",
    ],
)
