licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "allocator_impl_test",
    srcs = ["allocator_impl_test.cc"],
    deps = [
        "//source/common/stats:allocator_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "isolated_store_impl_test",
    srcs = ["isolated_store_impl_test.cc"],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:symbol_table_creator_lib",
    ],
)

envoy_cc_test(
    name = "metric_impl_test",
    srcs = ["metric_impl_test.cc"],
    deps = [
        "//source/common/stats:allocator_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/common/stats:utility_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_cc_test(
    name = "recent_lookups_test",
    srcs = ["recent_lookups_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/stats:recent_lookups_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_binary(
    name = "recent_lookups_speed_test",
    srcs = ["recent_lookups_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:recent_lookups_lib",
    ],
)

envoy_cc_test(
    name = "stat_merger_test",
    srcs = ["stat_merger_test.cc"],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stat_merger_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/common/stats:thread_local_store_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "stat_test_utility_lib",
    srcs = ["stat_test_utility.cc"],
    hdrs = ["stat_test_utility.h"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:symbol_table_creator_lib",
    ],
)

envoy_cc_test(
    name = "stat_test_utility_test",
    srcs = ["stat_test_utility_test.cc"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/stats:isolated_store_lib",
    ],
)

envoy_cc_test(
    name = "stats_matcher_impl_test",
    srcs = ["stats_matcher_impl_test.cc"],
    deps = [
        "//source/common/memory:stats_lib",
        "//source/common/stats:stats_matcher_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "refcount_ptr_test",
    srcs = ["refcount_ptr_test.cc"],
    deps = ["//include/envoy/stats:refcount_ptr_interface"],
)

envoy_cc_test(
    name = "symbol_table_impl_test",
    srcs = ["symbol_table_impl_test.cc"],
    external_deps = ["abseil_hash_testing"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:fake_symbol_table_lib",
        "//source/common/stats:symbol_table_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "stat_merger_fuzz_test",
    srcs = ["stat_merger_fuzz_test.cc"],
    corpus = "stat_merger_corpus",
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/stats:symbol_table_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "symbol_table_fuzz_test",
    srcs = ["symbol_table_fuzz_test.cc"],
    corpus = "symbol_table_corpus",
    deps = [
        ":stat_test_utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/common/stats:symbol_table_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_test_binary(
    name = "symbol_table_speed_test",
    srcs = ["symbol_table_speed_test.cc"],
    external_deps = [
        "abseil_strings",
        "benchmark",
    ],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:symbol_table_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
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
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tag_producer_impl_test",
    srcs = ["tag_producer_impl_test.cc"],
    deps = [
        "//source/common/stats:tag_producer_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "thread_local_store_test",
    srcs = ["thread_local_store_test.cc"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_binary(
    name = "thread_local_store_speed_test",
    srcs = ["thread_local_store_speed_test.cc"],
    external_deps = [
        "abseil_strings",
        "benchmark",
    ],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)
