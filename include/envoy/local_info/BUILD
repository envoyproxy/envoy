licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_info_interface",
    hdrs = ["local_info.h"],
    deps = [
        "//include/envoy/network:address_interface",
        "@envoy_api//envoy/api/v2:base_cc",
    ],
)
