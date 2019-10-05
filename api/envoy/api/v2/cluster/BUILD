load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "cluster",
    deps = [
        "//envoy/api/v2/core",
    ],
)

api_proto_library_internal(
    name = "circuit_breaker",
    srcs = ["circuit_breaker.proto"],
    visibility = [
        "//envoy/api/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:base",
    ],
)

api_proto_library_internal(
    name = "outlier_detection",
    srcs = ["outlier_detection.proto"],
    visibility = [
        "//envoy/api/v2:__pkg__",
    ],
)

api_proto_library_internal(
    name = "filter",
    srcs = ["filter.proto"],
    visibility = [
        "//envoy/api/v2:__pkg__",
    ],
)
