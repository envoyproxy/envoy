licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client_impl.cc"],
    hdrs = ["async_client_impl.h"],
    deps = [
        "//include/envoy/config:typed_metadata_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:context_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:message_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/router:router_ratelimit_interface",
        "//include/envoy/router:shadow_writer_interface",
        "//include/envoy/ssl:connection_interface",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/router:router_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
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
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "codec_helper_lib",
    hdrs = ["codec_helper.h"],
    deps = [
        "//include/envoy/http:codec_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "default_server_string_lib",
    hdrs = ["default_server_string.h"],
    deps = [
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "codec_wrappers_lib",
    hdrs = ["codec_wrappers.h"],
    deps = ["//include/envoy/http:codec_interface"],
)

envoy_cc_library(
    name = "codes_lib",
    srcs = ["codes.cc"],
    hdrs = ["codes.h"],
    deps = [
        ":headers_lib",
        ":utility_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "@envoy_api//envoy/type:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = ["context_impl.cc"],
    hdrs = ["context_impl.h"],
    deps = [
        ":codes_lib",
        "//include/envoy/http:context_interface",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_base_lib",
    srcs = ["conn_pool_base.cc"],
    hdrs = ["conn_pool_base.h"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//source/common/common:linked_object",
    ],
)

envoy_cc_library(
    name = "conn_manager_config_interface",
    hdrs = ["conn_manager_config.h"],
    deps = [
        ":date_provider_lib",
        "//include/envoy/config:config_provider_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/router:rds_interface",
        "//source/common/network:utility_lib",
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
        ":conn_manager_config_interface",
        ":exception_lib",
        ":header_map_lib",
        ":header_utility_lib",
        ":headers_lib",
        ":path_utility_lib",
        ":user_agent_lib",
        ":utility_lib",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:context_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/router:rds_interface",
        "//include/envoy/router:scopes_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/ssl:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:regex_lib",
        "//source/common/common:scope_tracker",
        "//source/common/common:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
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
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "exception_lib",
    hdrs = ["exception.h"],
    deps = ["//include/envoy/http:header_map_interface"],
)

envoy_cc_library(
    name = "hash_policy_lib",
    srcs = ["hash_policy.cc"],
    hdrs = ["hash_policy.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/http:hash_policy_interface",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_map_lib",
    srcs = ["header_map_impl.cc"],
    hdrs = ["header_map_impl.h"],
    deps = [
        ":headers_lib",
        "//include/envoy/http:header_map_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:empty_string",
        "//source/common/common:non_copyable",
        "//source/common/common:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "headers_lib",
    hdrs = ["headers.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/singleton:const_singleton",
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_library(
    name = "message_lib",
    srcs = ["message_impl.cc"],
    hdrs = ["message_impl.h"],
    deps = [
        ":header_map_lib",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:message_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:stack_array",
    ],
)

envoy_cc_library(
    name = "rest_api_fetcher_lib",
    srcs = ["rest_api_fetcher.cc"],
    hdrs = ["rest_api_fetcher.h"],
    deps = [
        ":message_lib",
        ":utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "user_agent_lib",
    srcs = ["user_agent.cc"],
    hdrs = ["user_agent.h"],
    deps = [
        ":headers_lib",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = [
        "abseil_optional",
        "http_parser",
    ],
    deps = [
        ":exception_lib",
        ":header_map_lib",
        ":headers_lib",
        ":message_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:query_params_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/grpc:status_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_utility_lib",
    srcs = ["header_utility.cc"],
    hdrs = ["header_utility.h"],
    external_deps = [
        "nghttp2",
    ],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:rds_json_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/type:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "path_utility_lib",
    srcs = ["path_utility.cc"],
    hdrs = ["path_utility.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/chromium_url",
        "//source/common/common:logger_lib",
    ],
)
