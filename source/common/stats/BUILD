load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "allocator_lib",
    srcs = ["allocator_impl.cc"],
    hdrs = ["allocator_impl.h"],
    deps = [
        ":metric_impl_lib",
        ":stat_merger_lib",
        "//envoy/stats:sink_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
        "//source/common/common:thread_synchronizer_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "custom_stat_namespaces_lib",
    srcs = ["custom_stat_namespaces_impl.cc"],
    hdrs = ["custom_stat_namespaces_impl.h"],
    deps = [
        "//envoy/stats:custom_stat_namespaces_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/common:thread_lib",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

envoy_cc_library(
    name = "deferred_creation",
    hdrs = ["deferred_creation.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:thread_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "histogram_lib",
    srcs = ["histogram_impl.cc"],
    hdrs = ["histogram_impl.h"],
    deps = [
        ":metric_impl_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:utility_lib",
        "@com_github_openhistogram_libcircllhist//:libcircllhist",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "isolated_store_lib",
    srcs = ["isolated_store_impl.cc"],
    hdrs = ["isolated_store_impl.h"],
    deps = [
        ":histogram_lib",
        ":null_counter_lib",
        ":null_gauge_lib",
        ":null_text_readout_lib",
        ":stats_lib",
        ":tag_utility_lib",
        "//envoy/stats:stats_macros",
        "//source/common/stats:allocator_lib",
    ],
)

envoy_cc_library(
    name = "metric_impl_lib",
    srcs = ["metric_impl.cc"],
    hdrs = ["metric_impl.h"],
    deps = [
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "tag_utility_lib",
    srcs = ["tag_utility.cc"],
    hdrs = ["tag_utility.h"],
    deps = [
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
        "//source/common/config:well_known_names",
    ],
)

envoy_cc_library(
    name = "null_counter_lib",
    hdrs = ["null_counter.h"],
    deps = [
        ":metric_impl_lib",
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "null_gauge_lib",
    hdrs = ["null_gauge.h"],
    deps = [
        ":metric_impl_lib",
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "null_text_readout_lib",
    hdrs = ["null_text_readout.h"],
    deps = [
        ":metric_impl_lib",
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "recent_lookups_lib",
    srcs = ["recent_lookups.cc"],
    hdrs = ["recent_lookups.h"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "stat_merger_lib",
    srcs = ["stat_merger.cc"],
    hdrs = ["stat_merger.h"],
    deps = [
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    deps = [
        ":histogram_lib",
        ":metric_impl_lib",
        ":symbol_table_lib",
        ":tag_extractor_lib",
        ":utility_lib",
        "//envoy/common:time_interface",
        "//envoy/server:options_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "symbol_table_lib",
    srcs = ["symbol_table.cc"],
    hdrs = ["symbol_table.h"],
    deps = [
        ":recent_lookups_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:mem_block_builder_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/container:inlined_vector",
    ],
)

envoy_cc_library(
    name = "tag_extractor_lib",
    srcs = ["tag_extractor_impl.cc"],
    hdrs = ["tag_extractor_impl.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:regex_lib",
    ],
)

envoy_cc_library(
    name = "tag_producer_lib",
    srcs = ["tag_producer_impl.cc"],
    hdrs = ["tag_producer_impl.h"],
    deps = [
        ":symbol_table_lib",
        ":tag_extractor_lib",
        ":utility_lib",
        "//envoy/stats:stats_interface",
        "//source/common/common:perf_annotation_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "@com_google_absl//absl/container:node_hash_set",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_matcher_lib",
    srcs = ["stats_matcher_impl.cc"],
    hdrs = ["stats_matcher_impl.h"],
    deps = [
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
        "//source/common/common:matchers_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":allocator_lib",
        ":histogram_lib",
        ":null_counter_lib",
        ":null_gauge_lib",
        ":null_text_readout_lib",
        ":stats_lib",
        ":stats_matcher_lib",
        ":tag_producer_lib",
        ":tag_utility_lib",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "timespan_lib",
    srcs = ["timespan_impl.cc"],
    hdrs = ["timespan_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":symbol_table_lib",
        "//envoy/stats:stats_interface",
    ],
)
