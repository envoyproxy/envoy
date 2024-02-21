load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "compressor_base",
    srcs = ["zstd_compressor_impl_base.cc"],
    hdrs = ["zstd_compressor_impl_base.h"],
    deps = [
        "//envoy/compression/compressor:compressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/compression/zstd/common:zstd_base_lib",
    ],
)
