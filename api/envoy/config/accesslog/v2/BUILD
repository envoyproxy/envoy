load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "als",
    srcs = ["als.proto"],
    deps = [
        "//envoy/api/v2/core:grpc_service",
    ],
)

api_proto_library(
    name = "file",
    srcs = ["file.proto"],
)
