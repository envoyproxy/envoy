load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "common",
    srcs = ["config.proto"],
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:http_uri",
    ],
)
