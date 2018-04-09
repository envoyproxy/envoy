load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "config_dump",
    srcs = ["config_dump.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:rds",
    ],
)
