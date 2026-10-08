load("@protobuf//bazel:proto_library.bzl", "proto_library")

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
        "@envoy_api//envoy/annotations:pkg",
        "@envoy_api//envoy/api/v2:pkg",
        "@envoy_api//envoy/api/v2/core:pkg",
        "@googleapis//google/api:annotations_proto",
        "@protobuf//:any_proto",
        "@xds//udpa/annotations:pkg",
    ],
)

filegroup(
    name = "golden_proto_files",
    srcs = glob(["*.gold"]),
    visibility = ["//visibility:public"],
)
