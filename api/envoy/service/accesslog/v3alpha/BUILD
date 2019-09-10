load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/data/accesslog/v3alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "als",
    srcs = ["als.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:grpc_service",
        "//envoy/data/accesslog/v3alpha:accesslog",
    ],
)
