load("//bazel:api_build_system.bzl", "api_proto_library", "api_go_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "health_check",
    srcs = ["health_check.proto"],
    deps = [
        "//envoy/type:percent",
    ],
)

api_go_proto_library(
    name = "health_check",
    proto = ":health_check",
    deps = [
        "//envoy/type:percent_go_proto",
    ],
)
