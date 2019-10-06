load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2:pkg",
        "//envoy/api/v2/auth:pkg",
        "//envoy/api/v2/core:pkg",
        "//envoy/config/metrics/v2:pkg",
        "//envoy/config/overload/v2alpha:pkg",
        "//envoy/config/ratelimit/v2:pkg",
        "//envoy/config/trace/v2:pkg",
    ],
)
