load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Kafka-mesh network filter.

envoy_extension_package()

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
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
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
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_response_lib",
        "//source/extensions/filters/network/kafka:tagged_fields_lib",
    ],
)
