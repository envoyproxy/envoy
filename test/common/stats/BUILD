load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "allocator_impl_test",
    srcs = ["allocator_impl_test.cc"],
    deps = [
        "//source/common/stats:allocator_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "custom_stat_namespaces_impl_test",
    srcs = ["custom_stat_namespaces_impl_test.cc"],
    deps = [
        "//source/common/stats:custom_stat_namespaces_lib",
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
    name = "histogram_impl_test",
    srcs = ["histogram_impl_test.cc"],
    deps = [
        "//source/common/stats:histogram_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "metric_impl_test",
    srcs = ["metric_impl_test.cc"],
    deps = [
        "//source/common/stats:allocator_lib",
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

envoy_cc_benchmark_binary(
    name = "recent_lookups_benchmark",
    srcs = ["recent_lookups_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:recent_lookups_lib",
    ],
)

envoy_benchmark_test(
    name = "recent_lookups_benchmark_test",
    benchmark_binary = "recent_lookups_benchmark",
)

envoy_cc_test(
    name = "stat_merger_test",
    srcs = ["stat_merger_test.cc"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stat_merger_lib",
        "//source/common/stats:thread_local_store_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "real_thread_test_base",
    srcs = ["real_thread_test_base.cc"],
    hdrs = ["real_thread_test_base.h"],
    deps = [
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:real_threads_test_helper_lib",
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
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:global_lib",
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
    deps = ["//envoy/stats:refcount_ptr_interface"],
)

envoy_cc_test(
    name = "symbol_table_impl_test",
    srcs = ["symbol_table_impl_test.cc"],
    external_deps = ["abseil_hash_testing"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/memory:stats_lib",
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
        "//source/common/stats:symbol_table_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "utility_fuzz_test",
    srcs = ["utility_fuzz_test.cc"],
    corpus = "utility_corpus",
    deps = [
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "make_elements_helper_lib",
    hdrs = ["make_elements_helper.h"],
)

envoy_cc_benchmark_binary(
    name = "symbol_table_benchmark",
    srcs = [
        "make_elements_helper.cc",
        "symbol_table_speed_test.cc",
    ],
    external_deps = [
        "abseil_strings",
        "benchmark",
    ],
    deps = [
        ":make_elements_helper_lib",
        ":stat_test_utility_lib",
        "//source/common/common:hash_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "deferred_creation_stats_test",
    srcs = ["deferred_creation_stats_test.cc"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/memory:stats_lib",
        "//source/common/stats:deferred_creation",
        "//source/common/stats:thread_local_store_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "deferred_creation_stats_benchmark",
    srcs = ["deferred_creation_stats_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":real_thread_test_base",
        "//source/common/common:random_generator_lib",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:deferred_creation",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/exe:process_wide_lib",
    ],
)

envoy_benchmark_test(
    name = "deferred_creation_stats_benchmark_test",
    size = "large",
    benchmark_binary = "deferred_creation_stats_benchmark",
)

envoy_benchmark_test(
    name = "symbol_table_benchmark_test",
    benchmark_binary = "symbol_table_benchmark",
)

envoy_cc_benchmark_binary(
    name = "stats_matcher_impl_benchmark",
    srcs = [
        "stats_matcher_impl_speed_test.cc",
    ],
    external_deps = [
        "abseil_strings",
        "benchmark",
    ],
    deps = [
        "//source/common/memory:stats_lib",
        "//source/common/stats:stats_matcher_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "stats_matcher_impl_benchmark_test",
    benchmark_binary = "stats_matcher_impl_benchmark",
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

envoy_cc_benchmark_binary(
    name = "tag_extractor_impl_benchmark",
    srcs = [
        "tag_extractor_impl_speed_test.cc",
    ],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/stats:tag_producer_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "tag_extractor_impl_benchmark_test",
    benchmark_binary = "tag_extractor_impl_benchmark",
)

envoy_cc_test(
    name = "thread_local_store_test",
    srcs = ["thread_local_store_test.cc"],
    deps = [
        ":real_thread_test_base",
        ":stat_test_utility_lib",
        "//source/common/memory:stats_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
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
        "//source/common/stats:stats_matcher_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "thread_local_store_speed_test_benchmark_test",
    benchmark_binary = "thread_local_store_speed_test",
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        ":stat_test_utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/stats:utility_lib",
    ],
)
