licenses(["notice"])  # Apache 2

load("//bazel:api_build_system.bzl", "api_proto_library")

api_proto_library(
    name = "rbac",
    srcs = ["rbac.proto"],
    deps = [
        "//envoy/api/v2/core:address",
    ],
)
