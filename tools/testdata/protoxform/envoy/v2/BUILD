load("@rules_proto//proto:defs.bzl", "proto_library")

licenses(["notice"])  # Apache 2

proto_library(
    name = "fix_protos",
    srcs = [
        "discovery_service.proto",
        "fully_qualified_names.proto",
        "oneof.proto",
        "package_move.proto",
        "sample.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/testdata/protoxform/external:external_protos",
        "@com_github_cncf_udpa//udpa/annotations:pkg",
        "@envoy_api//envoy/annotations:pkg",
        "@envoy_api//envoy/api/v2:pkg",
    ],
)

proto_library(
    name = "freeze_protos",
    srcs = [
        "active_non_terminal.proto",
        "active_terminal.proto",
        "frozen.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_cncf_udpa//udpa/annotations:pkg",
        "@envoy_api//envoy/annotations:pkg",
        "@envoy_api//envoy/api/v2:pkg",
    ],
)
