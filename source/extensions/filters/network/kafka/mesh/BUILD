load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)
load("//bazel:envoy_internal.bzl", "envoy_external_dep_path")

licenses(["notice"])  # Apache 2

# Kafka network filter.
# Broker filter public docs: docs/root/configuration/network_filters/kafka_broker_filter.rst

envoy_package()

envoy_cc_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "wip",
    deps = [
        ":clustering_lib",
        ":filter_lib",
        ":upstream_kafka_facade_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = [
        "filter.h",
    ],
    deps = [
        ":abstract_command_lib",
        ":clustering_lib",
        ":splitter_lib",
        ":upstream_kafka_facade_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
        "//source/extensions/filters/network/kafka:kafka_response_codec_lib",
    ],
)

envoy_cc_library(
    name = "splitter_lib",
    srcs = [
        "splitter.cc",
    ],
    hdrs = [
        "splitter.h",
    ],
    deps = [
        ":abstract_command_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_request_parser_lib",
        "//source/extensions/filters/network/kafka/mesh/command_handlers:api_versions_lib",
        "//source/extensions/filters/network/kafka/mesh/command_handlers:metadata_lib",
        "//source/extensions/filters/network/kafka/mesh/command_handlers:produce_lib",
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
    deps = [
        ":clustering_lib",
        ":upstream_kafka_facade_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_response_lib",
        "//source/extensions/filters/network/kafka:tagged_fields_lib",
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
    deps = [
        ":clustering_lib",
        ":upstream_kafka_client_lib",
        "//include/envoy/thread:thread_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "upstream_kafka_client_lib",
    srcs = [
        "upstream_kafka_client.cc",
    ],
    hdrs = [
        "upstream_kafka_client.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
        envoy_external_dep_path("librdkafka"),
    ],
)

envoy_cc_library(
    name = "clustering_lib",
    srcs = [
        "clustering.cc",
    ],
    hdrs = [
        "clustering.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ],
)
