licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "heap_stat_data_test",
    srcs = ["heap_stat_data_test.cc"],
    deps = [
        "//source/common/stats:heap_stat_data_lib",
        "//source/common/stats:stats_options_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "isolated_store_impl_test",
    srcs = ["isolated_store_impl_test.cc"],
    deps = [
        "//source/common/stats:isolated_store_lib",
    ],
)

envoy_cc_test(
    name = "source_impl_test",
    srcs = ["source_impl_test.cc"],
    deps = [
        "//source/common/stats:source_impl_lib",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "symbol_table_impl_test",
    srcs = ["symbol_table_impl_test.cc"],
    deps = [
        "//source/common/stats:symbol_table_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stats_matcher_impl_test",
    srcs = ["stats_matcher_impl_test.cc"],
    deps = [
        "//source/common/stats:stats_matcher_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "tag_extractor_impl_test",
    srcs = ["tag_extractor_impl_test.cc"],
    deps = [
        "//source/common/stats:tag_extractor_lib",
        "//source/common/stats:tag_producer_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "tag_producer_impl_test",
    srcs = ["tag_producer_impl_test.cc"],
    deps = [
        "//source/common/stats:tag_producer_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "thread_local_store_test",
    srcs = ["thread_local_store_test.cc"],
    deps = [
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:thread_local_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)
