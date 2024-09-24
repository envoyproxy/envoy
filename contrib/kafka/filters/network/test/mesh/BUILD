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
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:config_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread:thread_mocks",
    ],
)

envoy_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:filter_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "request_processor_unit_test",
    srcs = ["request_processor_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:request_processor_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "abstract_command_unit_test",
    srcs = ["abstract_command_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
    ],
)

envoy_cc_test(
    name = "upstream_kafka_facade_unit_test",
    srcs = ["upstream_kafka_facade_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:upstream_kafka_facade_lib",
        "//test/mocks/thread:thread_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "upstream_kafka_client_impl_unit_test",
    srcs = ["upstream_kafka_client_impl_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        ":kafka_mocks_lib",
        "//contrib/kafka/filters/network/source/mesh:upstream_kafka_client_impl_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test(
    name = "shared_consumer_manager_impl_unit_test",
    srcs = ["shared_consumer_manager_impl_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:shared_consumer_manager_impl_lib",
        "//test/mocks/thread:thread_mocks",
    ],
)

envoy_cc_test(
    name = "upstream_kafka_consumer_impl_unit_test",
    srcs = ["upstream_kafka_consumer_impl_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        ":kafka_mocks_lib",
        "//contrib/kafka/filters/network/source/mesh:upstream_kafka_consumer_impl_lib",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)

envoy_cc_test_library(
    name = "kafka_mocks_lib",
    srcs = [],
    hdrs = ["kafka_mocks.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//bazel/foreign_cc:librdkafka",
        "//contrib/kafka/filters/network/source/mesh:librdkafka_utils_lib",
    ],
)

envoy_cc_test(
    name = "upstream_config_unit_test",
    srcs = ["upstream_config_unit_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source/mesh:upstream_config_lib",
    ],
)
