load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "scope_provider_singleton_test",
    srcs = ["scope_provider_singleton_test.cc"],
    extension_names = ["envoy.access_loggers.stats"],
    deps = [
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/access_loggers/stats:scope_provider_singleton_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "stats_test",
    srcs = ["stats_test.cc"],
    extension_names = ["envoy.access_loggers.stats"],
    deps = [
        "//source/common/formatter:stream_info_formatter_extension_lib",
        "//source/common/stats:allocator_lib",
        "//source/common/stats:stat_match_input_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/extensions/access_loggers/stats:config",
        "//source/extensions/access_loggers/stats:stats_lib",
        "//source/extensions/matching/actions/transform_stat:config",
        "//test/common/memory:memory_test_utility_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/hash:hash_testing",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.access_loggers.stats"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/access_loggers/stats:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/matching/actions/transform_stat:config",
        "//test/integration:ads_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "stats_speed_test",
    srcs = [
        "stats_speed_test.cc",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:thread_local_store_lib",
        "//source/extensions/access_loggers/stats:stats_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "stats_speed_test_benchmark_test",
    benchmark_binary = "stats_speed_test",
)
