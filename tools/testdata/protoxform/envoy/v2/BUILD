licenses(["notice"])  # Apache 2

load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "protos",
    srcs = [
        "discovery_service.proto",
        "oneof.proto",
        "package_move.proto",
        "sample.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_cncf_udpa//udpa/annotations:pkg",
        "@envoy_api//envoy/annotations:pkg",
        "@envoy_api//envoy/api/v2:pkg",
    ],
)
