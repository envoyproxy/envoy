load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "orca_load_report",
    srcs = ["orca_load_report.proto"],
    visibility = [
        "//visibility:public",
    ],
)

api_go_proto_library(
    name = "orca_load_report",
    proto = ":orca_load_report",
)
