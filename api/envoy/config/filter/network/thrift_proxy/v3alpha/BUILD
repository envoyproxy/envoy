load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/route:pkg",
    ],
)

api_proto_library_internal(
    name = "thrift_proxy",
    srcs = [
        "route.proto",
        "thrift_proxy.proto",
    ],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/route",
    ],
)
