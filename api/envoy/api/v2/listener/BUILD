load("//bazel:api_build_system.bzl", "api_proto_library", "api_go_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "listener",
    srcs = ["listener.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/auth:cert",
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
    ],
)

api_go_proto_library(
    name = "listener",
    proto = ":listener",
    deps = [
        "//envoy/api/v2/auth:cert_go_proto",
        "//envoy/api/v2/core:address_go_proto",
        "//envoy/api/v2/core:base_go_proto",
    ],
)
