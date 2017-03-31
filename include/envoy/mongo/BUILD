package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "bson_includes",
    hdrs = ["bson.h"],
    deps = ["//include/envoy/buffer:buffer_includes"],
)

envoy_cc_library(
    name = "codec_includes",
    hdrs = ["codec.h"],
    deps = [":bson_includes"],
)
