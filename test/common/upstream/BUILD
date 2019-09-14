licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test(
    name = "cds_api_impl_test",
    srcs = ["cds_api_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:cds_api_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)

envoy_cc_test(
    name = "cluster_manager_impl_test",
    srcs = ["cluster_manager_impl_test.cc"],
    external_deps = [
        "abseil_optional",
    ],
    deps = [
        ":utility_lib",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/api:api_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/common/upstream:subset_lb_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:cds_cc",
    ],
)

envoy_cc_test(
    name = "conn_pool_map_impl_test",
    srcs = ["conn_pool_map_impl_test.cc"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
        "//source/common/upstream:conn_pool_map_impl_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "edf_scheduler_test",
    srcs = ["edf_scheduler_test.cc"],
    deps = ["//source/common/upstream:edf_scheduler_lib"],
)

envoy_cc_test(
    name = "eds_test",
    srcs = ["eds_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/upstream:eds_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:eds_cc",
    ],
)

envoy_cc_test(
    name = "health_checker_impl_test",
    srcs = ["health_checker_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "host_utility_test",
    srcs = ["host_utility_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:host_utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_balancer_impl_test",
    srcs = ["load_balancer_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_balancer_simulation_test",
    srcs = ["load_balancer_simulation_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "load_stats_reporter_test",
    srcs = ["load_stats_reporter_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/common/upstream:load_stats_reporter_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/api/v2/endpoint:endpoint_cc",
        "@envoy_api//envoy/api/v2/endpoint:load_report_cc",
    ],
)

envoy_cc_test(
    name = "hds_test",
    srcs = ["hds_test.cc"],
    deps = [
        "//source/common/upstream:health_discovery_service_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2/endpoint:load_report_cc",
        "@envoy_api//envoy/service/discovery/v2:hds_cc",
    ],
)

envoy_cc_test(
    name = "logical_dns_cluster_test",
    srcs = ["logical_dns_cluster_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:logical_dns_cluster_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "original_dst_cluster_test",
    srcs = ["original_dst_cluster_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:original_dst_cluster_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "outlier_detection_impl_test",
    srcs = ["outlier_detection_impl_test.cc"],
    external_deps = ["abseil_optional"],
    deps = [
        ":utility_lib",
        "//include/envoy/common:time_interface",
        "//source/common/network:utility_lib",
        "//source/common/upstream:outlier_detection_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "priority_conn_pool_map_impl_test",
    srcs = ["priority_conn_pool_map_impl_test.cc"],
    deps = [
        "//include/envoy/http:conn_pool_interface",
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
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/upstream:resource_manager_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "ring_hash_lb_test",
    srcs = ["ring_hash_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/router:router_interface",
        "//source/common/network:utility_lib",
        "//source/common/upstream:ring_hash_lb_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "maglev_lb_test",
    srcs = ["maglev_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/upstream:maglev_lb_lib",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_binary(
    name = "load_balancer_benchmark",
    testonly = 1,
    srcs = ["load_balancer_benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/memory:stats_lib",
        "//source/common/upstream:maglev_lb_lib",
        "//source/common/upstream:ring_hash_lb_lib",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:printers_lib",
    ],
)

envoy_cc_test(
    name = "subset_lb_test",
    srcs = ["subset_lb_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:subset_lb_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "upstream_impl_test",
    srcs = ["upstream_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        # TODO(mattklein123): Split this into 2 tests for each cluster.
        "//source/common/upstream:static_cluster_lib",
        "//source/common/upstream:strict_dns_cluster_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "cluster_factory_impl_test",
    srcs = ["cluster_factory_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/upstream:cluster_factory_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/integration/clusters:custom_static_cluster",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
