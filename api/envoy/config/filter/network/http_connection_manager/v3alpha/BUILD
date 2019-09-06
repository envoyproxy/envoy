load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha",
        "//envoy/api/v3alpha/core",
        "//envoy/config/filter/accesslog/v3alpha:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "http_connection_manager",
    srcs = ["http_connection_manager.proto"],
    deps = [
        "//envoy/api/v3alpha:rds",
        "//envoy/api/v3alpha:srds",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/api/v3alpha/core:protocol",
        "//envoy/config/filter/accesslog/v3alpha:accesslog",
        "//envoy/type:percent",
    ],
)
