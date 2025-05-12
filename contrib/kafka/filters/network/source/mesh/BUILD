load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Kafka-mesh network filter.
# Mesh filter public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/kafka_mesh_filter

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:windows": [],
        "//conditions:default": [
            ":filter_lib",
            ":shared_consumer_manager_impl_lib",
            ":upstream_config_lib",
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
        ":shared_consumer_manager_lib",
        ":upstream_config_lib",
        ":upstream_kafka_facade_lib",
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
        ":upstream_kafka_facade_lib",
        "//contrib/kafka/filters/network/source:kafka_request_codec_lib",
        "//contrib/kafka/filters/network/source:kafka_request_parser_lib",
        "//contrib/kafka/filters/network/source/mesh:shared_consumer_manager_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:api_versions_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:fetch_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:list_offsets_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:metadata_lib",
        "//contrib/kafka/filters/network/source/mesh/command_handlers:produce_lib",
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
        "//envoy/event:dispatcher_interface",
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
    name = "shared_consumer_manager_lib",
    srcs = [],
    hdrs = ["shared_consumer_manager.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":upstream_config_lib",
        ":upstream_kafka_consumer_lib",
    ],
)

envoy_cc_library(
    name = "shared_consumer_manager_impl_lib",
    srcs = ["shared_consumer_manager_impl.cc"],
    hdrs = ["shared_consumer_manager_impl.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":shared_consumer_manager_lib",
        ":upstream_config_lib",
        ":upstream_kafka_consumer_impl_lib",
        ":upstream_kafka_consumer_lib",
        "//contrib/kafka/filters/network/source:kafka_types_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "inbound_record_lib",
    srcs = [
    ],
    hdrs = [
        "inbound_record.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
    ],
)

envoy_cc_library(
    name = "outbound_record_lib",
    srcs = [
    ],
    hdrs = [
        "outbound_record.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
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
        ":outbound_record_lib",
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
        ":librdkafka_utils_impl_lib",
        ":outbound_record_lib",
        ":upstream_kafka_client_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "upstream_kafka_consumer_lib",
    srcs = [
    ],
    hdrs = [
        "upstream_kafka_consumer.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":inbound_record_lib",
    ],
)

envoy_cc_library(
    name = "upstream_kafka_consumer_impl_lib",
    srcs = [
        "upstream_kafka_consumer_impl.cc",
    ],
    hdrs = [
        "upstream_kafka_consumer_impl.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":librdkafka_utils_impl_lib",
        ":upstream_kafka_consumer_lib",
        "//contrib/kafka/filters/network/source:kafka_types_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
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
        "//envoy/common:exception_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "librdkafka_utils_lib",
    srcs = [
    ],
    hdrs = [
        "librdkafka_utils.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//bazel/foreign_cc:librdkafka",
        "//envoy/common:pure_lib",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "librdkafka_utils_impl_lib",
    srcs = [
        "librdkafka_utils_impl.cc",
    ],
    hdrs = [
        "librdkafka_utils_impl.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":librdkafka_utils_lib",
        "//bazel/foreign_cc:librdkafka",
        "//source/common/common:macros",
    ],
)
