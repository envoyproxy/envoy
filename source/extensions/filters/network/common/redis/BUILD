licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = ["//include/envoy/buffer:buffer_interface"],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":codec_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:stack_array",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "supported_commands_lib",
    hdrs = ["supported_commands.h"],
    deps = [
        "//source/common/common:macros",
    ],
)
