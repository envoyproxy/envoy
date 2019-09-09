load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v3alpha/core"],
)

api_proto_library(
    name = "alts",
    srcs = ["alts.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
    ],
)
