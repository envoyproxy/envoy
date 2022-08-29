load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
    ],
)
