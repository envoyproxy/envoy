licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_id_utils_interface",
    hdrs = ["request_id_utils.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
    ],
)
