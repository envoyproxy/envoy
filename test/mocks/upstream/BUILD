load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "cluster_info_mocks",
    srcs = ["cluster_info.cc"],
    hdrs = ["cluster_info.h"],
    deps = [
        ":transport_socket_match_mocks",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:thread_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http2:codec_stats_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "host_mocks",
    srcs = ["host.cc"],
    hdrs = ["host.h"],
    deps = [
        ":cluster_info_mocks",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/network:transport_socket_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/cluster/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "transport_socket_match_mocks",
    srcs = ["transport_socket_match.cc"],
    hdrs = ["transport_socket_match.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/stats:isolated_store_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "load_balancer_context_mock",
    srcs = ["load_balancer_context.cc"],
    hdrs = ["load_balancer_context.h"],
    deps = [
        "//include/envoy/upstream:load_balancer_interface",
    ],
)

envoy_cc_mock(
    name = "upstream_mocks",
    hdrs = ["mocks.h"],
    deps = [
        ":basic_resource_limit_mocks",
        ":cds_api_mocks",
        ":cluster_info_factory_mocks",
        ":cluster_manager_factory_mocks",
        ":cluster_manager_mocks",
        ":cluster_mocks",
        ":cluster_priority_set_mocks",
        ":cluster_real_priority_set_mocks",
        ":cluster_update_callbacks_handle_mocks",
        ":cluster_update_callbacks_mocks",
        ":health_check_event_logger_mocks",
        ":health_checker_mocks",
        ":host_set_mocks",
        ":load_balancer_context_mock",
        ":load_balancer_mocks",
        ":priority_set_mocks",
        ":retry_host_predicate_mocks",
        ":retry_priority_factory_mocks",
        ":retry_priority_mocks",
        ":test_retry_host_predicate_factory_mocks",
        ":thread_aware_load_balancer_mocks",
        ":thread_local_cluster_mocks",
        ":transport_socket_match_mocks",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/upstream:cluster_factory_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:health_checker_interface",
        "//include/envoy/upstream:load_balancer_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "host_set_mocks",
    srcs = ["host_set.cc"],
    hdrs = ["host_set.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:callback_impl_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

envoy_cc_mock(
    name = "priority_set_mocks",
    srcs = ["priority_set.cc"],
    hdrs = ["priority_set.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//test/mocks/upstream:host_set_mocks",
    ],
)

envoy_cc_mock(
    name = "retry_priority_mocks",
    srcs = ["retry_priority.cc"],
    hdrs = ["retry_priority.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
    ],
)

envoy_cc_mock(
    name = "retry_priority_factory_mocks",
    hdrs = ["retry_priority_factory.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
        "//test/mocks/upstream:retry_priority_mocks",
    ],
)

envoy_cc_mock(
    name = "cluster_mocks",
    srcs = ["cluster.cc"],
    hdrs = ["cluster.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//test/mocks/upstream:cluster_info_mocks",
    ],
)

envoy_cc_mock(
    name = "cluster_real_priority_set_mocks",
    srcs = ["cluster_real_priority_set.cc"],
    hdrs = ["cluster_real_priority_set.h"],
    deps = [
        "//test/mocks/upstream:cluster_mocks",
    ],
)

envoy_cc_mock(
    name = "cluster_priority_set_mocks",
    srcs = ["cluster_priority_set.cc"],
    hdrs = ["cluster_priority_set.h"],
    deps = [
        "//test/mocks/upstream:cluster_mocks",
        "//test/mocks/upstream:priority_set_mocks",
    ],
)

envoy_cc_mock(
    name = "load_balancer_mocks",
    srcs = ["load_balancer.cc"],
    hdrs = ["load_balancer.h"],
    deps = [
        "//include/envoy/upstream:load_balancer_interface",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_mock(
    name = "thread_aware_load_balancer_mocks",
    srcs = ["thread_aware_load_balancer.cc"],
    hdrs = ["thread_aware_load_balancer.h"],
    deps = [
        "//include/envoy/upstream:load_balancer_interface",
    ],
)

envoy_cc_mock(
    name = "thread_local_cluster_mocks",
    srcs = ["thread_local_cluster.cc"],
    hdrs = ["thread_local_cluster.h"],
    deps = [
        "//include/envoy/upstream:thread_local_cluster_interface",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:load_balancer_mocks",
    ],
)

envoy_cc_mock(
    name = "cluster_manager_factory_mocks",
    srcs = ["cluster_manager_factory.cc"],
    hdrs = ["cluster_manager_factory.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//test/mocks/secret:secret_mocks",
    ],
)

envoy_cc_mock(
    name = "cluster_update_callbacks_handle_mocks",
    srcs = ["cluster_update_callbacks_handle.cc"],
    hdrs = ["cluster_update_callbacks_handle.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_mock(
    name = "cluster_manager_mocks",
    srcs = ["cluster_manager.cc"],
    hdrs = ["cluster_manager.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//test/mocks/config:config_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:cluster_manager_factory_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
    ],
)

envoy_cc_mock(
    name = "health_checker_mocks",
    srcs = ["health_checker.cc"],
    hdrs = ["health_checker.h"],
    deps = [
        "//include/envoy/upstream:health_checker_interface",
    ],
)

envoy_cc_mock(
    name = "health_check_event_logger_mocks",
    hdrs = ["health_check_event_logger.h"],
    deps = [
        "//include/envoy/upstream:health_checker_interface",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "cds_api_mocks",
    srcs = ["cds_api.cc"],
    hdrs = ["cds_api.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_mock(
    name = "cluster_update_callbacks_mocks",
    srcs = ["cluster_update_callbacks.cc"],
    hdrs = ["cluster_update_callbacks.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_mock(
    name = "cluster_info_factory_mocks",
    srcs = ["cluster_info_factory.cc"],
    hdrs = ["cluster_info_factory.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_mock(
    name = "retry_host_predicate_mocks",
    srcs = ["retry_host_predicate.cc"],
    hdrs = ["retry_host_predicate.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
    ],
)

envoy_cc_mock(
    name = "test_retry_host_predicate_factory_mocks",
    hdrs = ["test_retry_host_predicate_factory.h"],
    deps = [
        "//include/envoy/upstream:retry_interface",
        "//test/mocks/upstream:retry_host_predicate_mocks",
    ],
)

envoy_cc_mock(
    name = "basic_resource_limit_mocks",
    srcs = ["basic_resource_limit.cc"],
    hdrs = ["basic_resource_limit.h"],
    deps = [
        "//include/envoy/common:resource_interface",
    ],
)
