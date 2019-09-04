load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v3alpha/core"],
)

api_proto_library(
    name = "health_check_event",
    srcs = ["health_check_event.proto"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
    ],
)
