load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Handlers for particular Kafka requests that are used by Kafka-mesh filter.

envoy_extension_package()

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
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/network/kafka:kafka_request_parser_lib",
        "//source/extensions/filters/network/kafka:kafka_response_parser_lib",
        "//source/extensions/filters/network/kafka:tagged_fields_lib",
        "//source/extensions/filters/network/kafka/mesh:abstract_command_lib",
    ],
)
