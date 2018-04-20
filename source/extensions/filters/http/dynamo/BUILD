licenses(["notice"])  # Apache 2
# AWS DynamoDB L7 HTTP filter (observability): https://aws.amazon.com/dynamodb/
# Public docs: docs/root/configuration/http_filters/dynamodb_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "dynamo_filter_lib",
    srcs = ["dynamo_filter.cc"],
    hdrs = ["dynamo_filter.h"],
    deps = [
        ":dynamo_request_parser_lib",
        ":dynamo_utility_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/runtime:runtime_interface",
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
        "//include/envoy/http:header_map_interface",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "dynamo_utility_lib",
    srcs = ["dynamo_utility.cc"],
    hdrs = ["dynamo_utility.h"],
    deps = ["//source/common/stats:stats_lib"],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":dynamo_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)
