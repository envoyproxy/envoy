licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "allocator_lib",
    srcs = ["allocator_impl.cc"],
    hdrs = ["allocator_impl.h"],
    deps = [
        ":metric_impl_lib",
        ":stat_merger_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "histogram_lib",
    srcs = ["histogram_impl.cc"],
    hdrs = ["histogram_impl.h"],
    external_deps = [
        "libcircllhist",
    ],
    deps = [
        ":metric_impl_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "isolated_store_lib",
    srcs = ["isolated_store_impl.cc"],
    hdrs = ["isolated_store_impl.h"],
    deps = [
        ":fake_symbol_table_lib",
        ":histogram_lib",
        ":null_counter_lib",
        ":null_gauge_lib",
        ":scope_prefixer_lib",
        ":stats_lib",
        ":store_impl_lib",
        ":symbol_table_creator_lib",
        "//include/envoy/stats:stats_macros",
        "//source/common/stats:allocator_lib",
    ],
)

envoy_cc_library(
    name = "metric_impl_lib",
    srcs = ["metric_impl.cc"],
    hdrs = ["metric_impl.h"],
    deps = [
        ":symbol_table_lib",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "null_counter_lib",
    hdrs = ["null_counter.h"],
    deps = [
        ":metric_impl_lib",
        ":symbol_table_lib",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "null_gauge_lib",
    hdrs = ["null_gauge.h"],
    deps = [
        ":metric_impl_lib",
        ":symbol_table_lib",
        "//include/envoy/stats:stats_interface",
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
    name = "store_impl_lib",
    hdrs = ["store_impl.h"],
    deps = [
        ":symbol_table_lib",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "scope_prefixer_lib",
    srcs = ["scope_prefixer.cc"],
    hdrs = ["scope_prefixer.h"],
    deps = [
        ":symbol_table_lib",
        ":utility_lib",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "stat_merger_lib",
    srcs = ["stat_merger.cc"],
    hdrs = ["stat_merger.h"],
    deps = [
        ":symbol_table_lib",
        "//include/envoy/stats:stats_interface",
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
        "//include/envoy/common:time_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/metrics/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "symbol_table_lib",
    srcs = ["symbol_table_impl.cc"],
    hdrs = ["symbol_table_impl.h"],
    external_deps = ["abseil_base"],
    deps = [
        ":recent_lookups_lib",
        "//include/envoy/stats:symbol_table_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:stack_array",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "symbol_table_creator_lib",
    srcs = ["symbol_table_creator.cc"],
    hdrs = ["symbol_table_creator.h"],
    external_deps = ["abseil_base"],
    deps = [
        ":fake_symbol_table_lib",
        ":symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "fake_symbol_table_lib",
    hdrs = ["fake_symbol_table_impl.h"],
    deps = [":symbol_table_lib"],
)

envoy_cc_library(
    name = "tag_extractor_lib",
    srcs = ["tag_extractor_impl.cc"],
    hdrs = ["tag_extractor_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:perf_annotation_lib",
        "//source/common/common:regex_lib",
    ],
)

envoy_cc_library(
    name = "tag_producer_lib",
    srcs = ["tag_producer_impl.cc"],
    hdrs = ["tag_producer_impl.h"],
    deps = [
        ":tag_extractor_lib",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:perf_annotation_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/metrics/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_matcher_lib",
    srcs = ["stats_matcher_impl.cc"],
    hdrs = ["stats_matcher_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:matchers_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/metrics/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":allocator_lib",
        ":null_counter_lib",
        ":null_gauge_lib",
        ":scope_prefixer_lib",
        ":stats_lib",
        ":stats_matcher_lib",
        ":tag_producer_lib",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "timespan_lib",
    srcs = ["timespan_impl.cc"],
    hdrs = ["timespan_impl.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [":symbol_table_lib"],
)
