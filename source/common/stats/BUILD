licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "heap_stat_data_lib",
    srcs = ["heap_stat_data.cc"],
    hdrs = ["heap_stat_data.h"],
    deps = [
        ":stat_data_allocator_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
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
        ":histogram_lib",
        ":stats_lib",
        ":stats_options_lib",
        "//include/envoy/stats:stats_macros",
        "//source/common/stats:heap_stat_data_lib",
    ],
)

envoy_cc_library(
    name = "metric_impl_lib",
    hdrs = ["metric_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "raw_stat_data_lib",
    srcs = ["raw_stat_data.cc"],
    hdrs = ["raw_stat_data.h"],
    deps = [
        ":stat_data_allocator_lib",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_library(
    name = "source_impl_lib",
    srcs = ["source_impl.cc"],
    hdrs = ["source_impl.h"],
    external_deps = [
        "abseil_optional",
    ],
    deps = [
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "stat_data_allocator_lib",
    hdrs = ["stat_data_allocator_impl.h"],
    deps = [
        ":metric_impl_lib",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    deps = [
        ":histogram_lib",
        ":metric_impl_lib",
        ":raw_stat_data_lib",
        ":source_impl_lib",
        ":stats_options_lib",
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
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_library(
    name = "symbol_table_lib",
    srcs = ["symbol_table_impl.cc"],
    hdrs = ["symbol_table_impl.h"],
    external_deps = ["abseil_base"],
    deps = [
        "//include/envoy/stats:symbol_table_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "stats_options_lib",
    hdrs = ["stats_options_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "tag_extractor_lib",
    srcs = ["tag_extractor_impl.cc"],
    hdrs = ["tag_extractor_impl.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:perf_annotation_lib",
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
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_library(
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":heap_stat_data_lib",
        ":stats_lib",
        ":tag_producer_lib",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
)
