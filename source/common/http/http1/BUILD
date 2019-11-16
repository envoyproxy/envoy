licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "header_formatter_lib",
    srcs = ["header_formatter.cc"],
    hdrs = ["header_formatter.h"],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":parser_interface",
        ":parser_factory_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:to_lower_table_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_helper_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:header_formatter_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:codec_wrappers_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

envoy_cc_library(
    name = "parser_interface",
    hdrs = ["parser.h"],
)

envoy_cc_library(
    name = "llhttp_lib",
    srcs = ["llhttp_parser.cc"],
    hdrs = ["llhttp_parser.h"],
    deps = [
        ":parser_interface",
        "//source/common/common:assert_lib",
    ],
    external_deps = [
        "llhttp",
    ],
)

envoy_cc_library(
    name = "legacy_http_parser_lib",
    srcs = ["legacy_http_parser.cc"],
    hdrs = ["legacy_http_parser.h"],
    deps = [
        ":parser_interface",
        "//source/common/common:assert_lib",
    ],
    external_deps = [
        "http_parser"
    ]
)

envoy_cc_library(
    name = "parser_factory_lib",
    srcs = ["parser_factory.cc"],
    hdrs = ["parser_factory.h"],
    deps = [
        ":parser_interface",
        ":legacy_http_parser_lib",
        ":llhttp_lib",
    ]
)
