load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "refcount_ptr_interface",
    hdrs = ["refcount_ptr.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "tag_interface",
    hdrs = ["tag.h"],
)

# TODO(jmarantz): atomize the build rules to match the include files.
envoy_cc_library(
    name = "stats_interface",
    hdrs = [
        "allocator.h",
        "histogram.h",
        "scope.h",
        "stats.h",
        "stats_matcher.h",
        "store.h",
        "tag_extractor.h",
        "tag_producer.h",
    ],
    external_deps = ["abseil_inlined_vector"],
    deps = [
        ":refcount_ptr_interface",
        ":tag_interface",
        "//envoy/common:interval_set_interface",
        "//envoy/common:optref_lib",
        "//envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "sink_interface",
    hdrs = ["sink.h"],
    deps = [
        ":primitive_stats_interface",
        ":stats_interface",
    ],
)

envoy_cc_library(
    name = "primitive_stats_interface",
    hdrs = [
        "primitive_stats.h",
    ],
    deps = [
        ":tag_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
    ],
)

envoy_cc_library(
    name = "timespan_interface",
    hdrs = ["timespan.h"],
)

envoy_cc_library(
    name = "stats_macros",
    hdrs = ["stats_macros.h"],
    deps = [
        ":stats_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "primitive_stats_macros",
    hdrs = ["primitive_stats_macros.h"],
    deps = [":primitive_stats_interface"],
)

envoy_cc_library(
    name = "custom_stat_namespaces_interface",
    hdrs = ["custom_stat_namespaces.h"],
)
