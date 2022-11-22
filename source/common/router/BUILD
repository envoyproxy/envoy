load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
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
    external_deps = ["abseil_optional"],
    deps = [
        ":config_utility_lib",
        ":context_lib",
        ":header_formatter_lib",
        ":header_parser_lib",
        ":metadatamatchcriteria_lib",
        ":reset_header_parser_lib",
        ":retry_state_lib",
        ":router_ratelimit_lib",
        ":tls_context_match_criteria_lib",
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
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:hash_policy_lib",
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
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
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
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:macros",
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
    name = "rds_lib",
    srcs = ["rds_impl.cc"],
    hdrs = ["rds_impl.h"],
    deps = [
        ":config_lib",
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
)

envoy_cc_library(
    name = "scoped_config_lib",
    srcs = ["scoped_config_impl.cc"],
    hdrs = ["scoped_config_impl.h"],
    external_deps = [
        "abseil_str_format",
    ],
    deps = [
        ":config_lib",
        "//envoy/router:rds_interface",
        "//envoy/router:scopes_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/protobuf:utility_lib",
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
    external_deps = ["abseil_optional"],
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
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
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
        ":config_lib",
        ":context_lib",
        ":debug_config_lib",
        ":header_parser_lib",
        ":retry_state_lib",
        ":upstream_codec_filter_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/grpc:status",
        "//envoy/http:codec_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:filter_factory_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:stateful_session_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/router:shadow_writer_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:scope_tracker",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:filter_chain_helper_lib",
        "//source/common/http:filter_manager_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_http_factory_context_lib",
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
        ":header_formatter_lib",
        "//envoy/http:header_evaluator",
        "//envoy/http:header_map_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_formatter_lib",
    srcs = ["header_formatter.cc"],
    hdrs = ["header_formatter.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/router:string_accessor_interface",
        "//envoy/stream_info:filter_state_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
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
