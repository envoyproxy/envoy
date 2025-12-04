load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "metadatamatchcriteria_lib",
    srcs = ["metadatamatchcriteria_impl.cc"],
    hdrs = ["metadatamatchcriteria_impl.h"],
    deps = [
        "//envoy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "tls_context_match_criteria_lib",
    srcs = ["tls_context_match_criteria_impl.cc"],
    hdrs = ["tls_context_match_criteria_impl.h"],
    deps = [
        "//envoy/router:router_interface",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config_impl.cc"],
    hdrs = ["config_impl.h"],
    deps = [
        ":config_utility_lib",
        ":context_lib",
        ":header_cluster_specifier_lib",
        ":header_parser_lib",
        ":matcher_visitor_lib",
        ":metadatamatchcriteria_lib",
        ":per_filter_config_lib",
        ":retry_policy_lib",
        ":retry_state_lib",
        ":router_ratelimit_lib",
        ":tls_context_match_criteria_lib",
        ":weighted_cluster_specifier_lib",
        "//envoy/config:typed_metadata_interface",
        "//envoy/http:header_map_interface",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/router:router_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:filter_config_interface",  # TODO(rodaine): break dependency on server
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:hash_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:packed_struct_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/http:hash_policy_lib",
        "//source/common/http:header_mutation_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:path_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:retry_factory_lib",
        "//source/extensions/early_data:default_early_data_policy_lib",
        "//source/extensions/path/match/uri_template:config",
        "//source/extensions/path/rewrite/uri_template:config",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "matcher_visitor_lib",
    srcs = ["matcher_visitor.cc"],
    hdrs = ["matcher_visitor.h"],
    deps = [
        "//source/common/http/matching:inputs_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "per_filter_config_lib",
    srcs = ["per_filter_config.cc"],
    hdrs = ["per_filter_config.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/config:utility_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "weighted_cluster_specifier_lib",
    srcs = ["weighted_cluster_specifier.cc"],
    hdrs = ["weighted_cluster_specifier.h"],
    deps = [
        ":config_utility_lib",
        ":delegating_route_lib",
        ":header_parser_lib",
        ":metadatamatchcriteria_lib",
        ":per_filter_config_lib",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/config:well_known_names",
        "//source/common/http:hash_policy_lib",
    ],
)

envoy_cc_library(
    name = "config_utility_lib",
    srcs = ["config_utility.cc"],
    hdrs = ["config_utility.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/upstream:resource_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:matchers_lib",
        "//source/common/config:datasource_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = ["context_impl.cc"],
    hdrs = ["context_impl.h"],
    deps = [
        "//envoy/router:context_interface",
        "//envoy/router:router_interface",
        "//envoy/stats:stats_macros",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "debug_config_lib",
    srcs = ["debug_config.cc"],
    hdrs = ["debug_config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "route_config_update_impl_lib",
    srcs = ["route_config_update_receiver_impl.cc"],
    hdrs = ["route_config_update_receiver_impl.h"],
    deps = [
        ":config_lib",
        "//envoy/router:rds_interface",
        "//envoy/router:route_config_update_info_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/rds:rds_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "vhds_lib",
    srcs = ["vhds.cc"],
    hdrs = ["vhds.h"],
    deps = [
        ":config_lib",
        "//envoy/config:subscription_interface",
        "//envoy/http:codes_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:rds_interface",
        "//envoy/router:route_config_provider_manager_interface",
        "//envoy/router:route_config_update_info_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:utility_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:route_config_update_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_provider_manager_lib",
    srcs = [
        "route_provider_manager.cc",
        "static_route_provider_impl.cc",
    ],
    hdrs = [
        "route_provider_manager.h",
        "static_route_provider_impl.h",
    ],
    deps = [
        ":config_lib",
        "//envoy/http:codes_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:rds_interface",
        "//envoy/router:route_config_provider_manager_interface",
        "//envoy/router:route_config_update_info_interface",
        "//source/common/router:route_config_update_impl_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    srcs = [
        "rds_impl.cc",
    ],
    hdrs = [
        "rds_impl.h",
    ],
    deps = [
        ":config_lib",
        ":route_provider_manager_lib",
        "//envoy/config:subscription_interface",
        "//envoy/http:codes_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:rds_interface",
        "//envoy/router:route_config_provider_manager_interface",
        "//envoy/router:route_config_update_info_interface",
        "//envoy/server:admin_interface",
        "//source/common/rds:rds_lib",
        "//source/common/router:route_config_update_impl_lib",
        "//source/common/router:vhds_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "scoped_config_lib",
    srcs = ["scoped_config_impl.cc"],
    hdrs = ["scoped_config_impl.h"],
    deps = [
        ":config_lib",
        "//envoy/router:rds_interface",
        "//envoy/router:scopes_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "scoped_rds_lib",
    srcs = ["scoped_rds.cc"],
    hdrs = ["scoped_rds.h"],
    deps = [
        ":rds_lib",
        ":scoped_config_lib",
        "//envoy/config:config_provider_interface",
        "//envoy/config:subscription_interface",
        "//envoy/router:route_config_provider_manager_interface",
        "//envoy/router:scopes_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:config_provider_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:xds_resource_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "retry_state_lib",
    srcs = ["retry_state_impl.cc"],
    hdrs = ["retry_state_impl.h"],
    deps = [
        "//envoy/event:timer_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:header_map_interface",
        "//envoy/router:router_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:backoff_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_to_downstream_impl_base",
    hdrs = [
        "upstream_to_downstream_impl_base.h",
    ],
    deps = [
        "//envoy/router:router_interface",
        "//source/common/http:response_decoder_impl_base",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = [
        "router.cc",
        "upstream_request.cc",
    ],
    hdrs = [
        "router.h",
        "upstream_request.h",
    ],
    deps = [
        ":context_lib",
        ":debug_config_lib",
        ":header_parser_lib",
        ":metadatamatchcriteria_lib",
        ":retry_state_lib",
        ":upstream_codec_filter_lib",
        ":upstream_to_downstream_impl_base",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/grpc:status",
        "//envoy/http:codes_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:filter_factory_interface",
        "//envoy/http:filter_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:router_filter_interface",
        "//envoy/router:shadow_writer_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:filter_chain_helper_lib",
        "//source/common/http:filter_manager_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:sidestream_watermark_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/orca:orca_load_metrics_lib",
        "//source/common/orca:orca_parser",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:upstream_factory_context_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_codec_filter_lib",
    srcs = [
        "upstream_codec_filter.cc",
    ],
    hdrs = [
        "upstream_codec_filter.h",
    ],
    deps = [
        ":config_lib",
        ":upstream_to_downstream_impl_base",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:empty_string",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "router_ratelimit_lib",
    srcs = ["router_ratelimit.cc"],
    hdrs = ["router_ratelimit.h"],
    deps = [
        ":config_utility_lib",
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/router:router_interface",
        "//envoy/router:router_ratelimit_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "shadow_writer_lib",
    srcs = ["shadow_writer_impl.cc"],
    hdrs = ["shadow_writer_impl.h"],
    deps = [
        "//envoy/router:shadow_writer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "header_parser_lib",
    srcs = [
        "header_parser.cc",
        "header_parser_utils.cc",
    ],
    hdrs = ["header_parser.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/http:header_evaluator",
        "//envoy/http:header_map_interface",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "reset_header_parser_lib",
    srcs = ["reset_header_parser.cc"],
    hdrs = ["reset_header_parser.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//envoy/router:router_interface",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "string_accessor_lib",
    hdrs = ["string_accessor_impl.h"],
    deps = [
        "//envoy/router:string_accessor_interface",
    ],
)

envoy_cc_library(
    name = "delegating_route_lib",
    srcs = ["delegating_route_impl.cc"],
    hdrs = ["delegating_route_impl.h"],
    deps = [
        "//envoy/router:router_interface",
        "//source/common/config:metadata_lib",
    ],
)

envoy_cc_library(
    name = "header_cluster_specifier_lib",
    srcs = ["header_cluster_specifier.cc"],
    hdrs = ["header_cluster_specifier.h"],
    deps = [
        ":delegating_route_lib",
        "//envoy/router:cluster_specifier_plugin_interface",
    ],
)

envoy_cc_library(
    name = "retry_policy_lib",
    srcs = ["retry_policy_impl.cc"],
    hdrs = ["retry_policy_impl.h"],
    deps = [
        ":reset_header_parser_lib",
        ":retry_state_lib",
        "//envoy/router:router_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/config:utility_lib",
        "//source/common/upstream:retry_factory_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
