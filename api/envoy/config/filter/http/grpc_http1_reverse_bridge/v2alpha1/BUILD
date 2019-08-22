load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "grpc_http1_reverse_bridge",
    srcs = glob(["*.proto"]),
)

api_proto_library(
    name = "config",
    srcs = ["config.proto"],
)
