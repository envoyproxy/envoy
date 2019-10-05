load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v2",
        "//envoy/api/v2/core",
        "//envoy/api/v2/route:pkg",
        "//envoy/data/tap/v2alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "common",
    srcs = ["common.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:grpc_service",
        "//envoy/api/v2/route",
    ],
)

api_proto_library_internal(
    name = "tap",
    srcs = ["tap.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:discovery",
        "//envoy/api/v2/core:base",
        "//envoy/data/tap/v2alpha:wrapper",
    ],
)

api_proto_library_internal(
    name = "tapds",
    srcs = ["tapds.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:discovery",
        "//envoy/api/v2/core:base",
        "//envoy/service/tap/v2alpha:common",
    ],
)
