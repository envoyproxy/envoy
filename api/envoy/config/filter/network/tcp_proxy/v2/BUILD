load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "tcp_proxy",
    srcs = ["tcp_proxy.proto"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
        "//envoy/config/filter/accesslog/v2:accesslog",
    ],
)
