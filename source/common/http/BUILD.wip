package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client_impl.cc"],
    hdrs = ["async_client_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/http:async_client_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/http:message_includes",
        "//include/envoy/router:router_includes",
        "//include/envoy/router:router_ratelimit_includes",
        "//include/envoy/router:shadow_writer_includes",
        "//include/envoy/ssl:connection_includes",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/http/access_log:request_info_lib",
        "//source/common/router:router_lib",
    ],
)

envoy_cc_library(
    name = "codec_client_lib",
    srcs = ["codec_client.cc"],
    hdrs = ["codec_client.h"],
    deps = [
        ":codec_wrappers_lib",
        ":exception_lib",
        ":utility_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/http:codec_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:filter_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "codec_helper_lib",
    hdrs = ["codec_helper.h"],
)

envoy_cc_library(
    name = "codec_wrappers_lib",
    hdrs = ["codec_wrappers.h"],
    deps = ["//include/envoy/http:codec_includes"],
)

envoy_cc_library(
    name = "codes_lib",
    srcs = ["codes.cc"],
    hdrs = ["codes.h"],
    deps = [
        ":headers_lib",
        ":utility_lib",
        "//include/envoy/http:codes_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/stats:stats_includes",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = [
        "conn_manager_impl.cc",
        "conn_manager_utility.cc",
    ],
    hdrs = [
        "conn_manager_impl.h",
        "conn_manager_utility.h",
    ],
    deps = [
        ":codes_lib",
        ":date_provider_lib",
        ":exception_lib",
        ":header_map_lib",
        ":headers_lib",
        ":user_agent_lib",
        ":utility_lib",
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:access_log_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:drain_decision_includes",
        "//include/envoy/network:filter_includes",
        "//include/envoy/router:rds_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/ssl:connection_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/tracing:http_tracer_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http/access_log:access_log_formatter_lib",
        "//source/common/http/access_log:request_info_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "date_provider_lib",
    srcs = ["date_provider_impl.cc"],
    hdrs = [
        "date_provider.h",
        "date_provider_impl.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/thread_local:thread_local_includes",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "exception_lib",
    hdrs = ["exception.h"],
    deps = ["//include/envoy/http:header_map_includes"],
)

envoy_cc_library(
    name = "header_map_lib",
    srcs = ["header_map_impl.cc"],
    hdrs = ["header_map_impl.h"],
    deps = [
        ":headers_lib",
        "//include/envoy/http:header_map_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:non_copyable",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "headers_lib",
    hdrs = ["headers.h"],
    deps = ["//include/envoy/http:header_map_includes"],
)

envoy_cc_library(
    name = "message_lib",
    srcs = ["message_impl.cc"],
    hdrs = ["message_impl.h"],
    deps = [
        ":header_map_lib",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/http:message_includes",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:non_copyable",
    ],
)

envoy_cc_library(
    name = "rest_api_fetcher_lib",
    srcs = ["rest_api_fetcher.cc"],
    hdrs = ["rest_api_fetcher.h"],
    deps = [
        ":message_lib",
        ":utility_lib",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/upstream:cluster_manager_includes",
        "//source/common/common:enum_to_int",
    ],
)

envoy_cc_library(
    name = "user_agent_lib",
    srcs = ["user_agent.cc"],
    hdrs = ["user_agent.h"],
    deps = [
        ":headers_lib",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":exception_lib",
        ":header_map_lib",
        ":headers_lib",
        "//include/envoy/http:codes_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/http:header_map_includes",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
    ],
)
