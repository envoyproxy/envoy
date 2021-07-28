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
    name = "cluster_manager_lib",
    srcs = ["cluster_manager_impl.cc"],
    hdrs = ["cluster_manager_impl.h"],
    deps = [
        ":cds_api_lib",
        ":load_balancer_lib",
        ":load_stats_reporter_lib",
        ":ring_hash_lb_lib",
        ":subset_lb_lib",
        "//envoy/api:api_interface",
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
        "//source/common/config:grpc_mux_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:alternate_protocols_cache",
        "//source/common/http:mixed_conn_pool",
        "//source/common/http/http1:conn_pool_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:context_lib",
        "//source/common/router:shadow_writer_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "//source/common/tcp:conn_pool_lib",
        "//source/common/upstream:priority_conn_pool_map_impl_lib",
        "//source/common/upstream:upstream_lib",
        "//source/common/quic:quic_stat_names_lib",
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
    name = "edf_scheduler_lib",
    hdrs = ["edf_scheduler.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "health_checker_base_lib",
    srcs = ["health_checker_base_impl.cc"],
    hdrs = ["health_checker_base_impl.h"],
    deps = [
        "//envoy/upstream:health_checker_interface",
        "//source/common/router:router_lib",
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
        ":health_checker_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
        # TODO(dio): Remove dependency to server.
        "//envoy/server:health_checker_config_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/upstream:host_utility_lib",
    ],
)

envoy_cc_library(
    name = "host_utility_lib",
    srcs = ["host_utility.cc"],
    hdrs = ["host_utility.h"],
    deps = ["//envoy/upstream:upstream_interface"],
)

envoy_cc_library(
    name = "load_balancer_lib",
    srcs = ["load_balancer_impl.cc"],
    hdrs = ["load_balancer_impl.h"],
    deps = [
        ":edf_scheduler_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_protos_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
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
        "//source/common/config:version_converter_lib",
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
        "//source/common/config:version_converter_lib",
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
    name = "logical_host_lib",
    srcs = ["logical_host.cc"],
    hdrs = ["logical_host.h"],
    deps = [
        ":upstream_includes",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "logical_dns_cluster_lib",
    srcs = ["logical_dns_cluster.cc"],
    hdrs = ["logical_dns_cluster.h"],
    deps = [
        ":cluster_factory_lib",
        ":logical_host_lib",
        ":upstream_includes",
        "//envoy/upstream:cluster_factory_interface",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "original_dst_cluster_lib",
    srcs = ["original_dst_cluster.cc"],
    hdrs = ["original_dst_cluster.h"],
    deps = [
        ":cluster_factory_lib",
        ":upstream_includes",
        "//envoy/secret:secret_manager_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//source/common/common:empty_string",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "outlier_detection_lib",
    srcs = ["outlier_detection_impl.cc"],
    hdrs = ["outlier_detection_impl.h"],
    deps = [
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
    name = "maglev_lb_lib",
    srcs = ["maglev_lb.cc"],
    hdrs = ["maglev_lb.h"],
    deps = [
        ":thread_aware_lb_lib",
        ":upstream_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ring_hash_lb_lib",
    srcs = ["ring_hash_lb.cc"],
    hdrs = ["ring_hash_lb.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        ":thread_aware_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "eds_lib",
    srcs = ["eds.cc"],
    hdrs = ["eds.h"],
    deps = [
        ":cluster_factory_lib",
        ":upstream_includes",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:locality_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:decoded_resource_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subset_lb_lib",
    srcs = ["subset_lb.cc"],
    hdrs = ["subset_lb.h"],
    deps = [
        ":load_balancer_lib",
        ":maglev_lb_lib",
        ":ring_hash_lb_lib",
        ":upstream_lib",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "upstream_lib",
    srcs = ["upstream_impl.cc"],
    deps = [
        ":eds_lib",
        ":health_checker_lib",
        # TODO(mattklein123): Move the clusters to extensions so they can be compiled out.
        ":logical_dns_cluster_lib",
        ":original_dst_cluster_lib",
        ":static_cluster_lib",
        ":strict_dns_cluster_lib",
        ":upstream_includes",
        ":transport_socket_match_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/ssl:context_interface",
        "//envoy/upstream:health_checker_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http2:codec_stats_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/network/common:utility_lib",
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
    name = "static_cluster_lib",
    srcs = ["static_cluster.cc"],
    hdrs = ["static_cluster.h"],
    deps = [
        ":cluster_factory_includes",
        ":upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "strict_dns_cluster_lib",
    srcs = ["strict_dns_cluster.cc"],
    hdrs = ["strict_dns_cluster.h"],
    deps = [
        ":cluster_factory_includes",
        ":upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
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
        ":outlier_detection_lib",
        ":resource_manager_lib",
        "//envoy/event:timer_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:dns_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:health_checker_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:locality_lib",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http2:codec_stats_lib",
        "//source/common/http/http3:codec_stats_lib",
        "//source/common/init:manager_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/upstreams/http:config",
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
