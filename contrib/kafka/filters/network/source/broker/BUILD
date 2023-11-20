load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Kafka-broker network filter.
# Broker filter public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/kafka_broker_filter

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_config_lib",
        ":filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/kafka_broker/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_config_lib",
    srcs = [
        "filter_config.cc",
    ],
    hdrs = [
        "filter_config.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/kafka_broker/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = [
        "filter.h",
    ],
    deps = [
        ":filter_config_lib",
        ":rewriter_lib",
        "//contrib/kafka/filters/network/source:kafka_metrics_lib",
        "//contrib/kafka/filters/network/source:kafka_request_codec_lib",
        "//contrib/kafka/filters/network/source:kafka_response_codec_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "rewriter_lib",
    srcs = ["rewriter.cc"],
    hdrs = [
        "rewriter.h",
    ],
    deps = [
        ":filter_config_lib",
        "//contrib/kafka/filters/network/source:kafka_response_codec_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)
