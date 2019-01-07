load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "quiche",
    srcs = ["quiche.proto"],
    deps = ["//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager"],
)
