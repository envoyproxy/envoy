licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["gzip_compressor_impl.cc"],
    hdrs = ["gzip_compressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/buffer:buffer_lib",
        "//include/envoy/compressor:compressor_interface",
    ],
)