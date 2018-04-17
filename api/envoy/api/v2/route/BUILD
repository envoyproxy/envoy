load("//bazel:api_build_system.bzl", "api_proto_library", "api_go_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "route",
    srcs = ["route.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/auth",
        "//envoy/api/v2/core:base",
        "//envoy/type:range",
    ],
)

api_go_proto_library(
    name = "route",
    proto = ":route",
    deps = [
        "//envoy/api/v2/auth:auth_go_proto",
        "//envoy/api/v2/core:base_go_proto",
        "//envoy/type:range_go_proto",
    ],
)
