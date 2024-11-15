load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
    "envoy_select_enable_http_datagrams",
    "envoy_select_nghttp2",
)
load(
    "//bazel:envoy_internal.bzl",
    "envoy_external_dep_path",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "async_client_lib",
    srcs = [
        "async_client_impl.cc",
    ],
    hdrs = [
        "async_client_impl.h",
    ],
    deps = [
        ":null_route_impl_lib",
        "//envoy/config:typed_metadata_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:async_client_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:context_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:message_interface",
        "//envoy/router:context_interface",
        "//envoy/router:router_interface",
        "//envoy/router:router_ratelimit_interface",
        "//envoy/router:shadow_writer_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/router:config_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:retry_factory_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "null_route_impl_lib",
    srcs = ["null_route_impl.cc"],
    hdrs = ["null_route_impl.h"],
    deps = [
        ":hash_policy_lib",
        "//envoy/router:router_interface",
        "//source/common/router:router_lib",
    ],
)

envoy_cc_library(
    name = "async_client_utility_lib",
    srcs = ["async_client_utility.cc"],
    hdrs = ["async_client_utility.h"],
    deps = [
        "//envoy/http:async_client_interface",
    ],
)

envoy_cc_library(
    name = "sidestream_watermark_lib",
    hdrs = ["sidestream_watermark.h"],
    deps = [
        "//envoy/http:async_client_interface",
    ],
)

envoy_cc_library(
    name = "character_set_validation_lib",
    hdrs = ["character_set_validation.h"],
)

envoy_cc_library(
    name = "codec_client_lib",
    srcs = ["codec_client.cc"],
    hdrs = ["codec_client.h"],
    deps = [
        ":codec_wrappers_lib",
        ":exception_lib",
        ":status_lib",
        ":utility_lib",
        "//envoy/event:deferred_deletable",
        "//envoy/http:codec_interface",
        "//envoy/http:header_validator_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:filter_lib",
        "//source/common/runtime:runtime_features_lib",
    ] + envoy_select_enable_http3([
        "//source/common/quic:client_codec_lib",
    ]),
)

envoy_cc_library(
    name = "codec_helper_lib",
    hdrs = ["codec_helper.h"],
    deps = [
        "//envoy/http:codec_interface",
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
    deps = ["//envoy/http:codec_interface"],
)

envoy_cc_library(
    name = "codes_lib",
    srcs = ["codes.cc"],
    hdrs = ["codes.h"],
    deps = [
        ":headers_lib",
        ":utility_lib",
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = ["context_impl.cc"],
    hdrs = ["context_impl.h"],
    deps = [
        ":codes_lib",
        ":user_agent_lib",
        "//envoy/http:context_interface",
    ],
)

envoy_cc_library(
    name = "dependency_manager",
    srcs = ["dependency_manager.cc"],
    hdrs = ["dependency_manager.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@envoy_api//envoy/extensions/filters/common/dependency/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_chain_helper_lib",
    srcs = ["filter_chain_helper.cc"],
    hdrs = ["filter_chain_helper.h"],
    deps = [
        ":dependency_manager",
        "//envoy/config:config_provider_manager_interface",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/router:route_config_provider_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/filter:config_discovery_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_base_lib",
    srcs = ["conn_pool_base.cc"],
    hdrs = ["conn_pool_base.h"],
    deps = [
        ":codec_client_lib",
        ":http_server_properties_cache",
        "//envoy/http:conn_pool_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/conn_pool:conn_pool_base_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

envoy_cc_library(
    name = "mixed_conn_pool",
    srcs = ["mixed_conn_pool.cc"],
    hdrs = ["mixed_conn_pool.h"],
    deps = [
        ":conn_pool_base_lib",
        ":http_server_properties_cache",
        "//source/common/http/http1:conn_pool_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/tcp:conn_pool_lib",
    ],
)

envoy_cc_library(
    name = "http3_status_tracker_impl_lib",
    srcs = ["http3_status_tracker_impl.cc"],
    hdrs = ["http3_status_tracker_impl.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:http_server_properties_cache_interface",
    ],
)

envoy_cc_library(
    name = "http_server_properties_cache",
    srcs = [
        "http_server_properties_cache_impl.cc",
        "http_server_properties_cache_manager_impl.cc",
    ],
    hdrs = [
        "http_server_properties_cache_impl.h",
        "http_server_properties_cache_manager_impl.h",
    ],
    deps = [
        ":http3_status_tracker_impl_lib",
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:http_server_properties_cache_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:resource_manager_interface",
        "//source/common/common:key_value_store_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "@com_github_google_quiche//:http2_core_alt_svc_wire_format_lib",
        "@com_github_google_quiche//:quic_platform",
        "@envoy_api//envoy/config/common/key_value/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_grid",
    srcs = ["conn_pool_grid.cc"],
    hdrs = ["conn_pool_grid.h"],
    deps = [
        ":http3_status_tracker_impl_lib",
        ":http_server_properties_cache",
        ":mixed_conn_pool",
        "//source/common/http/http3:conn_pool_lib",
        "//source/common/quic:quic_stat_names_lib",
    ],
)

envoy_cc_library(
    name = "conn_manager_config_interface",
    hdrs = ["conn_manager_config.h"],
    deps = [
        ":date_provider_lib",
        "//envoy/config:config_provider_interface",
        "//envoy/http:early_header_mutation_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_validator_interface",
        "//envoy/http:original_ip_detection_interface",
        "//envoy/http:request_id_extension_interface",
        "//envoy/router:rds_interface",
        "//envoy/router:scopes_interface",
        "//source/common/local_reply:local_reply_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/tracing:tracer_config_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_manager_lib",
    srcs = [
        "filter_manager.cc",
    ],
    hdrs = [
        "filter_manager.h",
    ],
    deps = [
        ":headers_lib",
        "//envoy/http:filter_interface",
        "//envoy/matcher:matcher_interface",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:linked_object",
        "//source/common/common:scope_tracked_object_stack",
        "//source/common/common:scope_tracker",
        "//source/common/grpc:common_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/local_reply:local_reply_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    srcs = [
        "route_config_update_requster.cc",
    ],
    hdrs = [
        "route_config_update_requster.h",
    ],
    deps = [
        "//envoy/router:rds_interface",
        "//source/common/router:scoped_rds_lib",
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
        ":filter_manager_lib",
        ":header_map_lib",
        ":header_utility_lib",
        ":headers_lib",
        ":path_utility_lib",
        ":status_lib",
        ":user_agent_lib",
        ":utility_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/common:scope_tracker_interface",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:api_listener_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:context_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:header_validator_errors",
        "//envoy/http:header_validator_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/network:filter_interface",
        "//envoy/router:scopes_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stats:timespan_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/common:perf_tracing_lib",
        "//source/common/common:regex_lib",
        "//source/common/common:scope_tracker",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/network:proxy_protocol_filter_state_lib",
        "//source/common/network:utility_lib",
        "//source/common/quic:quic_server_factory_stub_lib",
        "//source/common/router:config_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
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
        "//envoy/event:dispatcher_interface",
        "//envoy/http:header_map_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "exception_lib",
    hdrs = ["exception.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "hash_policy_lib",
    srcs = ["hash_policy.cc"],
    hdrs = ["hash_policy.h"],
    deps = [
        ":utility_lib",
        "//envoy/common:hashable_interface",
        "//envoy/http:hash_policy_interface",
        "//source/common/common:matchers_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_list_view_lib",
    srcs = ["header_list_view.cc"],
    hdrs = ["header_list_view.h"],
    deps = [
        "//envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "header_map_lib",
    srcs = ["header_map_impl.cc"],
    hdrs = ["header_map_impl.h"],
    deps = [
        ":headers_lib",
        "//envoy/http:header_map_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:compiled_string_map_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:empty_string",
        "//source/common/common:non_copyable",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "headers_lib",
    hdrs = ["headers.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/singleton:const_singleton",
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_library(
    name = "message_lib",
    hdrs = ["message_impl.h"],
    deps = [
        ":header_map_lib",
        "//envoy/http:header_map_interface",
        "//envoy/http:message_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:non_copyable",
    ],
)

envoy_cc_library(
    name = "user_agent_lib",
    srcs = ["user_agent.cc"],
    hdrs = ["user_agent.h"],
    deps = [
        ":headers_lib",
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stats:timespan_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "http_option_limits_lib",
    srcs = ["http_option_limits.cc"],
    hdrs = ["http_option_limits.h"],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":character_set_validation_lib",
        ":exception_lib",
        ":header_map_lib",
        ":headers_lib",
        ":http_option_limits_lib",
        ":message_lib",
        "//bazel/external/http_parser",
        "//envoy/common:regex_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:query_params_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/grpc:status_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_google_quiche//:http2_adapter_http2_protocol",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_utility_lib",
    srcs = ["header_utility.cc"],
    hdrs = ["header_utility.h"],
    deps = [
        ":character_set_validation_lib",
        ":header_map_lib",
        ":status_lib",
        ":utility_lib",
        "//envoy/common:matchers_interface",
        "//envoy/common:regex_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:header_validator_interface",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:regex_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_google_quiche//:http2_adapter",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ] + envoy_select_enable_http_datagrams([
        "@com_github_google_quiche//:quiche_common_structured_headers_lib",
    ]) + envoy_select_nghttp2([envoy_external_dep_path("nghttp2")]),
)

envoy_cc_library(
    name = "path_utility_lib",
    srcs = ["path_utility.cc"],
    hdrs = ["path_utility.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/common:logger_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_google_absl//absl/types:optional",
        "@com_googlesource_googleurl//url",
    ],
)

envoy_cc_library(
    name = "request_id_extension_lib",
    srcs = [
        "request_id_extension_impl.cc",
    ],
    hdrs = [
        "request_id_extension_impl.h",
    ],
    deps = [
        "//envoy/http:request_id_extension_interface",
        "//envoy/server:request_id_extension_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "status_lib",
    srcs = ["status.cc"],
    hdrs = ["status.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//source/common/common:assert_lib",
        "@com_google_absl//absl/status",
    ],
)

envoy_cc_library(
    name = "header_mutation_lib",
    srcs = ["header_mutation.cc"],
    hdrs = ["header_mutation.h"],
    deps = [
        ":header_map_lib",
        ":utility_lib",
        "//envoy/http:header_evaluator",
        "//source/common/router:header_parser_lib",
        "@envoy_api//envoy/config/common/mutation_rules/v3:pkg_cc_proto",
    ],
)
