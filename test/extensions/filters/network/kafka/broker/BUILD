licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_unit_test",
    srcs = ["config_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_broker_config_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/extensions/filters/network/kafka:kafka_broker_filter_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_protocol_test",
    srcs = ["filter_protocol_test.cc"],
    extension_name = "envoy.filters.network.kafka_broker",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/network/kafka:kafka_broker_filter_lib",
        "//test/extensions/filters/network/kafka:buffer_based_test_lib",
        "//test/extensions/filters/network/kafka:message_utilities",
        "//test/test_common:test_time_lib",
    ],
)
