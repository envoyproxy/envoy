load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:header_utility_lib",
        "@envoy_api//envoy/extensions/filters/common/matcher/v3:pkg_cc_proto",
    ],
)
