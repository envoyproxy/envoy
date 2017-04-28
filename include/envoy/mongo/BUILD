licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "bson_interface",
    hdrs = ["bson.h"],
    deps = ["//include/envoy/buffer:buffer_interface"],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [":bson_interface"],
)
