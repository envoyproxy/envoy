load("//bazel:api_build_system.bzl", "api_go_grpc_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "rls",
    srcs = ["rls.proto"],
    visibility = [
        "//envoy/config/bootstrap/v2:__pkg__",
    ],
    deps = [
        "//envoy/api/v2/core:grpc_service",
    ],
)

api_go_grpc_library(
    name = "rls",
    proto = ":rls",
    deps = [
        "//envoy/api/v2/core:grpc_service_go_proto",
    ],
)
