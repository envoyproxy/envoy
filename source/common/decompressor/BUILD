licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "decompressor_lib",
    srcs = ["zlib_decompressor_impl.cc"],
    hdrs = ["zlib_decompressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//include/envoy/decompressor:decompressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
    ],
)
