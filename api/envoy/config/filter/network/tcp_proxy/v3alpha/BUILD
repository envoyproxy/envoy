load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/config/filter/accesslog/v3alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "tcp_proxy",
    srcs = ["tcp_proxy.proto"],
    deps = [
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/config/filter/accesslog/v3alpha:accesslog",
    ],
)
