load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "ext_authz",
    srcs = ["ext_authz.proto"],
    deps = [
        "//envoy/api/v2/core:grpc_service",
        "//envoy/api/v2/core:http_uri",
    ],
)
