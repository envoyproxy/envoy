load("@rules_proto//proto:defs.bzl", "proto_library")

licenses(["notice"])  # Apache 2

proto_library(
    name = "freeze_protos",
    srcs = ["frozen.proto"],
    visibility = ["//visibility:public"],
    deps = ["@com_github_cncf_udpa//udpa/annotations:pkg"],
)
