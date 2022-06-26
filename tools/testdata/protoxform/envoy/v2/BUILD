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
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:any_proto",
        "@envoy_api//envoy/annotations:pkg",
        "@envoy_api//envoy/api/v2:pkg",
        "@envoy_api//envoy/api/v2/core:pkg",
    ],
)

filegroup(
    name = "golden_proto_files",
    srcs = glob(["*.gold"]),
    visibility = ["//visibility:public"],
)
