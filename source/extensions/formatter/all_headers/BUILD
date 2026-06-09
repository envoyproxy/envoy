load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "all_headers_lib",
    srcs = ["all_headers.cc"],
    hdrs = ["all_headers.h"],
    deps = [
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/formatter/all_headers:all_headers_lib",
        "@envoy_api//envoy/extensions/formatter/all_headers/v3:pkg_cc_proto",
    ],
)
