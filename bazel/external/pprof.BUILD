load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")


proto_library(
    name = "profile_proto",
    srcs = ["proto/profile.proto"],
)

cc_proto_library(
    name = "profile_proto_cc",
    deps = [":profile_proto"],
    visibility = ["//visibility:public"],
)
