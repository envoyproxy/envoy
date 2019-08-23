load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "tap_config",
    deps = [
        "//envoy/api/v2/core",
        "//envoy/service/tap/v2alpha:tap_service",
    ],
)

api_proto_library_internal(
    name = "common",
    srcs = ["common.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2/core:config_source",
        "//envoy/service/tap/v2alpha:common",
    ],
)
