licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "decompressor_interface",
    hdrs = ["decompressor.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
    ],
)
