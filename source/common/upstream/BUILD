load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "cds_api_helper_lib",
    srcs = ["cds_api_helper.cc"],
    hdrs = ["cds_api_helper.h"],
    deps = [
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:resource_name_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cds_api_lib",
    srcs = ["cds_api_impl.cc"],
    hdrs = ["cds_api_impl.h"],
    deps = [
        ":cds_api_helper_lib",
        "//envoy/config:subscription_interface",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "od_cds_api_lib",
    srcs = ["od_cds_api_impl.cc"],
    hdrs = ["od_cds_api_impl.h"],
    deps = [
        ":cds_api_helper_lib",
        "//envoy/config:subscription_interface",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cluster_discovery_manager_lib",
    srcs = ["cluster_discovery_manager.cc"],
    hdrs = ["cluster_discovery_manager.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "cluster_manager_lib",
    srcs = ["cluster_manager_impl.cc"],
    hdrs = ["cluster_manager_impl.h"],
    deps = [
        ":cds_api_lib",
        ":cluster_discovery_manager_lib",
        ":host_utility_lib",
        ":load_balancer_lib",
        ":load_stats_reporter_lib",
        ":od_cds_api_lib",
        "//envoy/api:api_interface",
        "//envoy/config:xds_resources_delegate_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codes_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:dns_interface",
        "//envoy/router:context_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/config:custom_config_validators_lib",
        "//source/common/config:null_grpc_mux_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:http_server_properties_cache",
        "//source/common/http:mixed_conn_pool",
        "//source/common/http/http1:conn_pool_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/router:context_lib",
        "//source/common/router:shadow_writer_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "//source/common/tcp:async_tcp_client_lib",
        "//source/common/tcp:conn_pool_lib",
        "//source/common/upstream:priority_conn_pool_map_impl_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/server:factory_context_base_impl_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/http/http3:conn_pool_lib",
        "//source/common/http:conn_pool_grid",
    ]),
)

envoy_cc_library(
    name = "cluster_update_tracker_lib",
    srcs = ["cluster_update_tracker.cc"],
    hdrs = ["cluster_update_tracker.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "retry_factory_lib",
    hdrs = ["retry_factory.h"],
    deps = [
        "//envoy/upstream:retry_interface",
    ],
)

envoy_cc_library(
    name = "conn_pool_map",
    hdrs = ["conn_pool_map.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/upstream:resource_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:debug_recursion_checker_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_map_impl_lib",
    hdrs = ["conn_pool_map_impl.h"],
    deps = [
        ":conn_pool_map",
    ],
)

envoy_cc_library(
    name = "priority_conn_pool_map",
    hdrs = ["priority_conn_pool_map.h"],
    deps = [
        ":conn_pool_map",
        "//envoy/event:dispatcher_interface",
        "//envoy/upstream:resource_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:debug_recursion_checker_lib",
    ],
)

envoy_cc_library(
    name = "priority_conn_pool_map_impl_lib",
    hdrs = ["priority_conn_pool_map_impl.h"],
    deps = [
        ":conn_pool_map_impl_lib",
        ":priority_conn_pool_map",
    ],
)

envoy_cc_library(
    name = "scheduler_lib",
    hdrs = [
        "edf_scheduler.h",
        "wrsq_scheduler.h",
    ],
    deps = [
        "//envoy/common:random_generator_interface",
        "//envoy/upstream:scheduler_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "health_checker_event_logger_lib",
    srcs = ["health_checker_event_logger.cc"],
    hdrs = ["health_checker_event_logger.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/server:health_checker_config_interface",
        "//envoy/upstream:health_check_event_sink_interface",
        "//envoy/upstream:health_checker_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "health_checker_lib",
    srcs = ["health_checker_impl.cc"],
    hdrs = ["health_checker_impl.h"],
    external_deps = [
        "grpc_health_proto",
    ],
    deps = [
        ":health_checker_event_logger_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
        # TODO(dio): Remove dependency to server.
        "//envoy/server:health_checker_config_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/router:router_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/upstream:host_utility_lib",
    ],
)

envoy_cc_library(
    name = "host_utility_lib",
    srcs = ["host_utility.cc"],
    hdrs = ["host_utility.h"],
    deps = [
        "//envoy/stats:primitive_stats_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/config:well_known_names",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_library(
    name = "subset_lb_config_lib",
    srcs = ["subset_lb_config.cc"],
    hdrs = ["subset_lb_config.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/common/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_balancer_lib",
    srcs = ["load_balancer_impl.cc"],
    hdrs = ["load_balancer_impl.h"],
    deps = [
        ":scheduler_lib",
        ":subset_lb_config_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_protos_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/least_request/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/random/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_balancer_factory_base_lib",
    hdrs = ["load_balancer_factory_base.h"],
    deps = [
        ":load_balancer_lib",
    ],
)

envoy_cc_library(
    name = "load_stats_reporter_lib",
    srcs = ["load_stats_reporter.cc"],
    hdrs = ["load_stats_reporter.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "locality_endpoint_lib",
    hdrs = ["locality_endpoint.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "health_discovery_service_lib",
    srcs = ["health_discovery_service.cc"],
    hdrs = ["health_discovery_service.h"],
    deps = [
        ":health_checker_lib",
        ":locality_endpoint_lib",
        ":upstream_includes",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:backoff_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/network:resolver_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "outlier_detection_lib",
    srcs = ["outlier_detection_impl.cc"],
    hdrs = ["outlier_detection_impl.h"],
    deps = [
        ":upstream_includes",
        "//envoy/access_log:access_log_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:outlier_detection_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_manager_lib",
    hdrs = ["resource_manager_impl.h"],
    deps = [
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:resource_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:basic_resource_lib",
    ],
)

envoy_cc_library(
    name = "thread_aware_lb_lib",
    srcs = ["thread_aware_lb_impl.cc"],
    hdrs = ["thread_aware_lb_impl.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":load_balancer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_lib",
    srcs = ["upstream_impl.cc"],
    deps = [
        ":cluster_factory_lib",
        ":default_local_address_selector_factory",
        ":health_checker_lib",
        # TODO(mattklein123): Move the clusters to extensions so they can be compiled out.
        ":upstream_includes",
        ":transport_socket_match_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "//source/common/stats:deferred_creation",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/upstream/local_address_selector/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/registry",
        "//envoy/ssl:context_interface",
        "//envoy/upstream:health_checker_interface",
        "//source/common/common:dns_utils_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http2:codec_stats_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:happy_eyeballs_connection_impl_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/server:transport_socket_config_lib",
    ],
)

envoy_cc_library(
    name = "transport_socket_match_lib",
    srcs = ["transport_socket_match_impl.cc"],
    deps = [
        ":upstream_includes",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_includes",
    hdrs = [
        "transport_socket_match_impl.h",
        "upstream_impl.h",
    ],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":load_balancer_lib",
        ":resource_manager_lib",
        ":upstream_factory_context_lib",
        "//envoy/event:timer_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:dns_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:health_checker_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:locality_lib",
        "//envoy/upstream:outlier_detection_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:packed_struct_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http2:codec_stats_lib",
        "//source/common/http/http3:codec_stats_lib",
        "//source/common/init:manager_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "//source/common/stats:deferred_creation",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/upstreams/http:config",
        "//source/extensions/upstreams/tcp:config",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cluster_factory_lib",
    srcs = ["cluster_factory_impl.cc"],
    deps = [
        ":cluster_factory_includes",
        ":health_checker_lib",
        ":upstream_includes",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/ssl:context_interface",
        "//envoy/upstream:health_checker_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cluster_factory_includes",
    hdrs = ["cluster_factory_impl.h"],
    deps = [
        ":load_balancer_lib",
        ":outlier_detection_lib",
        ":resource_manager_lib",
        ":upstream_includes",
        "//envoy/event:timer_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:dns_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:health_checker_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:locality_lib",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_factory_context_lib",
    hdrs = ["upstream_factory_context_impl.h"],
    deps = [
        "//envoy/init:manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "default_local_address_selector",
    srcs = [
        "default_local_address_selector.cc",
    ],
    hdrs = [
        "default_local_address_selector.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "default_local_address_selector_factory",
    srcs = [
        "default_local_address_selector_factory.cc",
    ],
    hdrs = [
        "default_local_address_selector_factory.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":default_local_address_selector",
        "//envoy/network:address_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/upstream:upstream_interface",
        "//source/common/network:resolver_lib",
        "@envoy_api//envoy/config/upstream/local_address_selector/v3:pkg_cc_proto",
    ],
    # Ensure this factory in the source is always linked in.
    alwayslink = 1,
)
