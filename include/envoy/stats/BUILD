licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

# TODO(jmarantz): atomize the build rules to match the include files.
envoy_cc_library(
    name = "stats_interface",
    hdrs = [
        "histogram.h",
        "scope.h",
        "sink.h",
        "source.h",
        "stat_data_allocator.h",
        "stats.h",
        "stats_options.h",
        "store.h",
        "tag.h",
        "tag_extractor.h",
        "tag_producer.h",
    ],
    deps = ["//include/envoy/common:interval_set_interface"],
)

envoy_cc_library(
    name = "timespan",
    hdrs = ["timespan.h"],
    deps = [
        ":stats_interface",
        "//include/envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "stats_macros",
    hdrs = ["stats_macros.h"],
    deps = [":stats_interface"],
)
