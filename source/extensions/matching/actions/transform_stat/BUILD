load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transform_stat_lib",
    srcs = ["transform_stat.cc"],
    hdrs = ["transform_stat.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "@envoy_api//envoy/extensions/matching/actions/transform_stat/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":transform_stat_lib",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/matching/actions/transform_stat/v3:pkg_cc_proto",
    ],
)
