load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_headers_lib",
    hdrs = ["ratelimit_headers.h"],
    deps = [
        "//source/common/http:header_map_lib",
    ],
)
