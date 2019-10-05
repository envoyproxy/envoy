load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/service/tap/v3alpha:pkg",
    ],
)

api_proto_library_internal(
    name = "common",
    srcs = ["common.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/service/tap/v3alpha:common",
    ],
)
