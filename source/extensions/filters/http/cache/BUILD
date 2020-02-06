licenses(["notice"])  # Apache 2

## Pluggable HTTP cache filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "http_cache_utils_lib",
    srcs = ["http_cache_utils.cc"],
    hdrs = ["http_cache_utils.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/http:header_map_interface",
    ],
)
