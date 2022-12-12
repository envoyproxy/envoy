load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Handlers for particular Kafka requests that are used by Kafka-mesh filter.

envoy_cc_library(
    name = "produce_lib",
    srcs = [
        "produce.cc",
    ],
    hdrs = [
        "produce.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":produce_record_extractor_lib",
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source:kafka_response_parser_lib",
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
        "//contrib/kafka/filters/network/source/mesh:outbound_record_lib",
        "//contrib/kafka/filters/network/source/mesh:upstream_kafka_facade_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "produce_record_extractor_lib",
    srcs = [
        "produce_record_extractor.cc",
    ],
    hdrs = [
        "produce_record_extractor.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source/mesh:outbound_record_lib",
    ],
)

envoy_cc_library(
    name = "list_offsets_lib",
    srcs = [
        "list_offsets.cc",
    ],
    hdrs = [
        "list_offsets.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source:kafka_response_parser_lib",
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    srcs = [
        "metadata.cc",
    ],
    hdrs = [
        "metadata.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source:kafka_response_parser_lib",
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
        "//contrib/kafka/filters/network/source/mesh:upstream_config_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "api_versions_lib",
    srcs = [
        "api_versions.cc",
    ],
    hdrs = [
        "api_versions.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source:kafka_response_parser_lib",
        "//contrib/kafka/filters/network/source:tagged_fields_lib",
        "//contrib/kafka/filters/network/source/mesh:abstract_command_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)
