load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Kafka-mesh network filter.

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = [
        "filter.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":abstract_command_lib",
        ":request_processor_lib",
        ":upstream_config_lib",
        "//contrib/kafka/filters/network/source:kafka_request_codec_lib",
        "//contrib/kafka/filters/network/source:kafka_response_codec_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "request_processor_lib",
    srcs = [
        "request_processor.cc",
    ],
    hdrs = [
        "request_processor.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":abstract_command_lib",
        ":upstream_config_lib",
        "//contrib/kafka/filters/network/source:kafka_request_codec_lib",
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:api_versions_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:metadata_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "abstract_command_lib",
    srcs = [
        "abstract_command.cc",
    ],
    hdrs = [
        "abstract_command.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source:kafka_response_lib",
        "//contrib/kafka/filters/network/source:tagged_fields_lib",
    ],
)

envoy_cc_library(
    name = "upstream_kafka_facade_lib",
    srcs = [
        "upstream_kafka_facade.cc",
    ],
    hdrs = [
        "upstream_kafka_facade.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":upstream_config_lib",
        ":upstream_kafka_client_impl_lib",
        ":upstream_kafka_client_lib",
        "//envoy/thread:thread_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "upstream_kafka_client_lib",
    srcs = [
    ],
    hdrs = [
        "upstream_kafka_client.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
    ],
)

envoy_cc_library(
    name = "upstream_kafka_client_impl_lib",
    srcs = [
        "upstream_kafka_client_impl.cc",
    ],
    hdrs = [
        "upstream_kafka_client_impl.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":upstream_kafka_client_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "upstream_config_lib",
    srcs = [
    ],
    hdrs = [
        "upstream_config.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
    ],
)
