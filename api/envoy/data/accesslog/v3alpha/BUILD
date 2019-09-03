load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

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

api_go_proto_library(
    name = "accesslog",
    proto = ":accesslog",
    deps = [
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
    ],
)
