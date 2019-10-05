load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v3alpha",
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/route:pkg",
        "//envoy/data/tap/v3alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "common",
    srcs = ["common.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:grpc_service",
        "//envoy/api/v3alpha/route",
    ],
)

api_proto_library_internal(
    name = "tap",
    srcs = ["tap.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha:discovery",
        "//envoy/api/v3alpha/core:base",
        "//envoy/data/tap/v3alpha:wrapper",
    ],
)

api_proto_library_internal(
    name = "tapds",
    srcs = ["tapds.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha:discovery",
        "//envoy/api/v3alpha/core:base",
        "//envoy/service/tap/v3alpha:common",
    ],
)
