load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transform_stat_lib",
    srcs = ["transform_stat.cc"],
    hdrs = ["transform_stat.h"],
    visibility = [
        "//source/extensions/access_loggers/stats:__subpackages__",
        "//test/extensions/matching/actions/transform_stat:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/stats:stats_interface",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/matching/actions/transform_stat/v3:pkg_cc_proto",
    ],
)
