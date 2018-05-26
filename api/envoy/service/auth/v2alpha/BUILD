load("//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "attribute_context",
    srcs = [
        "attribute_context.proto",
    ],
    deps = [
        "//envoy/api/v2/core:address",
    ],
)

api_proto_library(
    name = "external_auth",
    srcs = [
        "external_auth.proto",
    ],
    has_services = 1,
    deps = [
        ":attribute_context",
    ],
)
