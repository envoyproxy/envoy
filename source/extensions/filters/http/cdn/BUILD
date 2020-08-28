load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cdn_loop_parser",
    srcs = ["cdn_loop_parser.cc"],
    hdrs = ["cdn_loop_parser.h"],
    deps = ["//source/common/common:statusor_lib"],
)

envoy_cc_library(
    name = "cdn_loop_utils",
    srcs = ["cdn_loop_utils.cc"],
    hdrs = ["cdn_loop_utils.h"],
    deps = [
        ":cdn_loop_parser",
        "//source/common/common:statusor_lib",
    ],
)
