load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# AWS DynamoDB L7 HTTP filter (observability): https://aws.amazon.com/dynamodb/
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/dynamodb_filter

envoy_contrib_package()

envoy_cc_library(
    name = "dynamo_filter_lib",
    srcs = ["dynamo_filter.cc"],
    hdrs = ["dynamo_filter.h"],
    deps = [
        ":dynamo_request_parser_lib",
        ":dynamo_stats_lib",
        "//envoy/http:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
    ],
)

envoy_cc_library(
    name = "dynamo_request_parser_lib",
    srcs = ["dynamo_request_parser.cc"],
    hdrs = ["dynamo_request_parser.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":dynamo_filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/dynamo/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dynamo_stats_lib",
    srcs = ["dynamo_stats.cc"],
    hdrs = ["dynamo_stats.h"],
    deps = [
        ":dynamo_request_parser_lib",
        "//envoy/stats:stats_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)
