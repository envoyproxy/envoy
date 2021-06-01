load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)
load("//bazel:envoy_internal.bzl", "envoy_external_dep_path")

licenses(["notice"])  # Apache 2

# Kafka-mesh network filter.
# Mesh filter public docs: docs/root/configuration/listeners/network_filters/kafka_mesh_filter.rst

envoy_extension_package()

envoy_cc_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:windows": [],
        "//conditions:default": [
            ":upstream_config_lib",
            ":filter_lib",
            ":upstream_kafka_facade_lib",
        ],
    }),
)

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
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_request_codec_lib",
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
    tags = ["skip_on_windows"],
    deps = [
        ":upstream_config_lib",
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
    tags = ["skip_on_windows"],
    deps = [
        ":upstream_config_lib",
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
    tags = ["skip_on_windows"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
        envoy_external_dep_path("librdkafka"),
    ],
)

envoy_cc_library(
    name = "upstream_config_lib",
    srcs = [
        "upstream_config.cc",
    ],
    hdrs = [
        "upstream_config.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ],
)
