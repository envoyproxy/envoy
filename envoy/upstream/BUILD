load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "cluster_manager_interface",
    hdrs = ["cluster_manager.h"],
    external_deps = [
        "abseil_node_hash_map",
    ],
    deps = [
        ":health_checker_interface",
        ":load_balancer_interface",
        ":thread_local_cluster_interface",
        ":upstream_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/config:grpc_mux_interface",
        "//envoy/config:subscription_factory_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/http:async_client_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:persistent_quic_info_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:options_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/thread_local:thread_local_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "health_checker_interface",
    hdrs = ["health_checker.h"],
    deps = [
        ":upstream_interface",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "health_check_host_monitor_interface",
    hdrs = ["health_check_host_monitor.h"],
)

envoy_cc_library(
    name = "host_description_interface",
    hdrs = ["host_description.h"],
    deps = [
        ":health_check_host_monitor_interface",
        ":outlier_detection_interface",
        ":resource_manager_interface",
        "//envoy/network:address_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/stats:primitive_stats_macros",
        "//envoy/stats:stats_macros",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_balancer_interface",
    hdrs = ["load_balancer.h"],
    deps = [
        ":upstream_interface",
        "//envoy/router:router_interface",
        "//envoy/upstream:types_interface",
    ],
)

envoy_cc_library(
    name = "load_balancer_type_interface",
    hdrs = ["load_balancer_type.h"],
    deps = [
        "//source/common/protobuf",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "locality_lib",
    hdrs = ["locality.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "outlier_detection_interface",
    hdrs = ["outlier_detection.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/common:time_interface",
        "@envoy_api//envoy/data/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "retry_interface",
    hdrs = ["retry.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/upstream:types_interface",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "resource_manager_interface",
    hdrs = ["resource_manager.h"],
    deps = ["//envoy/common:resource_interface"],
)

envoy_cc_library(
    name = "thread_local_cluster_interface",
    hdrs = ["thread_local_cluster.h"],
    deps = [
        ":load_balancer_interface",
        ":upstream_interface",
        "//envoy/http:async_client_interface",
    ],
)

envoy_cc_library(
    name = "types_interface",
    hdrs = ["types.h"],
    deps = [
        "//source/common/common:phantom",
    ],
)

envoy_cc_library(
    name = "upstream_interface",
    hdrs = ["upstream.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":health_check_host_monitor_interface",
        ":load_balancer_type_interface",
        ":locality_lib",
        ":resource_manager_interface",
        "//envoy/common:callback",
        "//envoy/config:subscription_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:filter_factory_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/ssl:context_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/upstream:types_interface",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cluster_factory_interface",
    hdrs = ["cluster_factory.h"],
    deps = [
        ":cluster_manager_interface",
        ":health_check_host_monitor_interface",
        ":load_balancer_type_interface",
        ":locality_lib",
        ":resource_manager_interface",
        ":upstream_interface",
        "//envoy/common:callback",
        "//envoy/config:typed_config_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/http:codec_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/ssl:context_interface",
        "//envoy/ssl:context_manager_interface",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "scheduler_interface",
    hdrs = ["scheduler.h"],
    deps = [
    ],
)
