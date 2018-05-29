load("//bazel:api_build_system.bzl", "api_go_grpc_library", "api_go_proto_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "lrs",
    srcs = ["lrs.proto"],
    has_services = 1,
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/endpoint:load_report",
    ],
)

api_go_grpc_library(
    name = "lrs",
    proto = ":lrs",
    deps = [
        "//envoy/api/v2/core:base_go_proto",
        "//envoy/api/v2/endpoint:load_report_go_proto",
    ],
)
