load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "als",
    srcs = ["als.proto"],
    deps = [
        "//envoy/api/v2/core:grpc_service",
    ],
)

api_proto_library_internal(
    name = "file",
    srcs = ["file.proto"],
)

api_go_proto_library(
    name = "als",
    proto = ":als",
    deps = ["//envoy/api/v2/core:grpc_service_go_proto"],
)
