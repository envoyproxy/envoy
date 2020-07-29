load("@rules_proto//proto:defs.bzl", "proto_library")

licenses(["notice"])  # Apache 2

proto_library(
    name = "fix_protos",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/testdata/protoxform/envoy/v2:fix_protos",
    ],
)

proto_library(
    name = "freeze_protos",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/testdata/protoxform/envoy/active_non_terminal/v2:freeze_protos",
        "//tools/testdata/protoxform/envoy/active_terminal/v2:freeze_protos",
        "//tools/testdata/protoxform/envoy/frozen/v2:freeze_protos",
        "//tools/testdata/protoxform/envoy/frozen/v3:freeze_protos",
    ],
)
