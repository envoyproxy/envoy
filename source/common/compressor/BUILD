licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["compressor_impl.cc"],
    hdrs = ["compressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//include/envoy/compressor:compressor_interface",
        "//source/common/buffer:buffer_lib",
    ],
)
