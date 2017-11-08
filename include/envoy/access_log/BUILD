licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "access_log_interface",
    hdrs = ["access_log.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/common:time_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:upstream_interface",
    ],
)
