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
    deps = [
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

envoy_cc_library(
    name = "store_interface",
    hdrs = ["store.h"],
    deps = [
        ":scope_interface",
        ":stats_interface",
        ":stats_matcher_interface",
        ":tag_producer_interface",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "scope_interface",
    hdrs = ["scope.h"],
    deps = [
        ":histogram_interface",
        ":refcount_ptr_interface",
        ":stats_matcher_interface",
        ":tag_interface",
        "//envoy/common:pure_lib",
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "stats_matcher_interface",
    hdrs = ["stats_matcher.h"],
    deps = [
        "//envoy/common:pure_lib",
    ],
)

envoy_cc_library(
    name = "histogram_interface",
    hdrs = ["histogram.h"],
    deps = [
        ":refcount_ptr_interface",
        ":stats_interface",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "tag_extractor_interface",
    hdrs = ["tag_extractor.h"],
    deps = [
        ":tag_interface",
        "//envoy/common:interval_set_interface",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "tag_producer_interface",
    hdrs = ["tag_producer.h"],
    deps = [
        ":tag_interface",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "stats_interface",
    hdrs = [
        "stats.h",
    ],
    deps = [
        ":refcount_ptr_interface",
        ":tag_interface",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
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
