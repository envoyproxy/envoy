load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["xfcc_value.cc"],
    hdrs = ["xfcc_value.h"],
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
