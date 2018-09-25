licenses(["notice"])  # Apache 2

# Kafka network filter.
# Public docs: docs/root/configuration/network_filters/kafka_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
)

envoy_cc_library(
    name = "kafka_request_codec_lib",
    srcs = ["request_codec.cc"],
    hdrs = [
        "codec.h",
        "request_codec.h",
    ],
    deps = [
        ":kafka_request_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "kafka_request_lib",
    srcs = ["kafka_request.cc"],
    hdrs = ["kafka_request.h"],
    deps = [
        ":parser_lib",
        ":serialization_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "parser_lib",
    hdrs = ["parser.h"],
    deps = [
        ":kafka_protocol_lib",
        ":message_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "message_lib",
    hdrs = [
        "message.h",
    ],
    deps = [
    ],
)

envoy_cc_library(
    name = "serialization_lib",
    hdrs = ["serialization.h"],
    deps = [
        ":kafka_protocol_lib",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_cc_library(
    name = "kafka_protocol_lib",
    hdrs = [
        "kafka_protocol.h",
        "kafka_types.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/common:macros",
    ],
)
