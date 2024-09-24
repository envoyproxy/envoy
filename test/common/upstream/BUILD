load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "od_cds_api_impl_test",
    srcs = ["od_cds_api_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/config:subscription_interface",
        "//source/common/stats:isolated_store_lib",
        "//source/common/upstream:od_cds_api_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:missing_cluster_notifier_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cds_api_impl_test",
    srcs = ["cds_api_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:cds_api_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_discovery_manager_test",
    srcs = ["cluster_discovery_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/upstream:cluster_discovery_manager_lib",
        "//test/mocks/upstream:thread_local_cluster_mocks",
    ],
)

envoy_cc_test(
    name = "deferred_cluster_initialization_test",
    srcs = ["deferred_cluster_initialization_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_cluster_manager",
        "//envoy/upstream:cluster_manager_interface",
        "//source/extensions/clusters/eds:eds_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/mocks/config:config_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/base",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_manager_impl_test",
    size = "large",
    srcs = ["cluster_manager_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_cluster_manager",
        "//source/common/router:context_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/extensions/clusters/eds:eds_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/config_subscription/grpc:grpc_collection_subscription_lib",
        "//source/extensions/config_subscription/grpc:grpc_subscription_lib",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//source/extensions/load_balancing_policies/maglev:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//source/extensions/network/dns_resolver/cares:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/upstreams/http/generic:config",
        "//test/config:v2_link_hacks",
        "//test/integration/load_balancers:custom_lb_policy",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/upstream:cds_api_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:cluster_real_priority_set_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/mocks/upstream:health_checker_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:od_cds_api_mocks",
        "//test/mocks/upstream:thread_aware_load_balancer_mocks",
        "//test/test_common:test_runtime_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_update_tracker_test",
    srcs = ["cluster_update_tracker_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/upstream:cluster_update_tracker_lib",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
    ],
)

envoy_cc_test(
    name = "conn_pool_map_impl_test",
    srcs = ["conn_pool_map_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:conn_pool_interface",
        "//source/common/upstream:conn_pool_map_impl_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "wrsq_scheduler_test",
    srcs = ["wrsq_scheduler_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/upstream:scheduler_lib",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "edf_scheduler_test",
    srcs = ["edf_scheduler_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/upstream:scheduler_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "health_check_fuzz_utils_lib",
    srcs = [
        "health_check_fuzz_test_utils.cc",
    ],
    hdrs = [
        "health_check_fuzz_test_utils.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/extensions/health_checkers/grpc:health_checker_lib",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//test/common/http:common_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
    ],
)

envoy_cc_test(
    name = "health_checker_impl_test",
    srcs = [
        "health_checker_impl_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/health_check/event_sinks/file:file_sink_lib",
        "//source/extensions/health_checkers/grpc:health_checker_lib",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//test/common/http:common_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_check/event_sinks/file/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "host_stats_test",
    srcs = ["host_stats_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/upstream:host_description_interface",
    ],
)

envoy_cc_test(
    name = "host_utility_test",
    srcs = ["host_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:host_utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/test_common:stats_utility_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "load_balancer_context_base_test",
    srcs = ["load_balancer_context_base_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/upstream:load_balancer_context_base_lib",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:priority_set_mocks",
    ],
)

envoy_cc_test(
    name = "load_balancer_simulation_test",
    srcs = ["load_balancer_simulation_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "load_stats_reporter_test",
    srcs = ["load_stats_reporter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stats:stats_lib",
        "//source/common/upstream:load_stats_reporter_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "hds_test",
    srcs = ["hds_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/tls:context_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/common/upstream:transport_socket_match_lib",
        "//source/extensions/health_checkers/grpc:health_checker_lib",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/upstream:cluster_info_factory_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/health/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "outlier_detection_impl_test",
    srcs = ["outlier_detection_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//envoy/common:time_interface",
        "//source/common/network:utility_lib",
        "//source/common/upstream:outlier_detection_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_checker_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "priority_conn_pool_map_impl_test",
    srcs = ["priority_conn_pool_map_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:conn_pool_interface",
        "//source/common/upstream:priority_conn_pool_map_impl_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "resource_manager_impl_test",
    srcs = ["resource_manager_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/upstream:upstream_interface",
        "//source/common/upstream:resource_manager_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "transport_socket_matcher_test",
    srcs = ["transport_socket_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/api:api_interface",
        "//source/common/config:metadata_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/upstream:transport_socket_match_lib",
        "//source/server:transport_socket_config_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_impl_test",
    srcs = ["upstream_impl_test.cc"],
    data = ["//test/common/tls/test_data:certs"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        ":test_local_address_selector",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "//envoy/api:api_interface",
        "//envoy/http:codec_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        # TODO(mattklein123): Split this into 2 tests for each cluster.
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/http_11_proxy:upstream_config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/upstreams/http:config",
        "//source/extensions/upstreams/tcp:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:health_checker_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/mocks/upstream:thread_aware_load_balancer_mocks",
        "//test/mocks/upstream:typed_load_balancer_factory_mocks",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//source/extensions/load_balancing_policies/maglev:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/ring_hash:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ] + envoy_select_enable_http3([
        "//source/common/quic:quic_transport_socket_factory_lib",
    ]),
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "test_cluster_manager",
    hdrs = ["test_cluster_manager.h"],
    deps = [
        ":utility_lib",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/api:api_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tls:context_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cluster_factory_impl_test",
    srcs = ["cluster_factory_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":utility_lib",
        "//envoy/api:api_interface",
        "//envoy/http:codec_interface",
        "//envoy/upstream:cluster_factory_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:options_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "health_check_fuzz_lib",
    srcs = ["health_check_fuzz.cc"],
    hdrs = ["health_check_fuzz.h"],
    rbe_pool = "2core",
    deps = [
        ":health_check_fuzz_proto_cc_proto",
        ":health_check_fuzz_utils_lib",
        ":utility_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_proto_library(
    name = "health_check_fuzz_proto",
    srcs = ["health_check_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "health_check_fuzz_test",
    srcs = ["health_check_fuzz_test.cc"],
    corpus = "health_check_corpus",
    rbe_pool = "2core",
    deps = [
        ":health_check_fuzz_lib",
        ":health_check_fuzz_proto_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "scheduler_benchmark",
    srcs = ["scheduler_benchmark.cc"],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/upstream:scheduler_lib",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_cc_test(
    name = "default_local_address_selector_test",
    size = "small",
    srcs = ["default_local_address_selector_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/common:base_includes",
        "//envoy/network:socket_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:default_local_address_selector_factory",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "local_address_selector_integration_test",
    size = "large",
    srcs = ["local_address_selector_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "test_local_address_selector",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/upstream/local_address_selector/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "test_local_address_selector",
    hdrs = [
        "test_local_address_selector.h",
    ],
    deps = [
        "//envoy/upstream:upstream_interface",
        "@com_google_absl//absl/types:optional",
    ],
)
