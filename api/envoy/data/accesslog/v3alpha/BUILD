load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v3alpha/core"],
)

api_proto_library_internal(
    name = "accesslog",
    srcs = ["accesslog.proto"],
    visibility = [
        "//envoy/service/accesslog/v3alpha:__pkg__",
    ],
    deps = [
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
    ],
)
