load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_unit_test",
    srcs = ["config_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_broker_config_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_broker_filter_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_protocol_test",
    srcs = ["filter_protocol_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_broker_filter_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/extensions/filters/network/kafka:buffer_based_test_lib",
        "//test/extensions/filters/network/kafka:message_utilities",
        "//test/test_common:test_time_lib",
    ],
)
