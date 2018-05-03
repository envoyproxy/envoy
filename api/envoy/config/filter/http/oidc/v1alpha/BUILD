load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "oidc",
    srcs = ["config.proto"],
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:http_uri",
        "//envoy/config/filter/http/common/v1alpha:common",
    ],
)
