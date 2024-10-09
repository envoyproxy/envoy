load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_unit_test",
    srcs = ["config_unit_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/kafka/filters/network/source/broker:config_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/kafka/filters/network/source/broker:filter_lib",
        "//envoy/event:timer_interface",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "filter_protocol_test",
    srcs = ["filter_protocol_test.cc"],
    deps = [
        ":mock_filter_config_test_lib",
        "//contrib/kafka/filters/network/source/broker:filter_lib",
        "//contrib/kafka/filters/network/test:buffer_based_test_lib",
        "//contrib/kafka/filters/network/test:message_utilities",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "rewriter_unit_test",
    srcs = ["rewriter_unit_test.cc"],
    deps = [
        ":mock_filter_config_test_lib",
        "//contrib/kafka/filters/network/source/broker:rewriter_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_test_library(
    name = "mock_filter_config_test_lib",
    srcs = [],
    hdrs = ["mock_filter_config.h"],
    deps = [
        "//contrib/kafka/filters/network/source/broker:filter_config_lib",
    ],
)
