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
    deps = [
        ":health_checker_interface",
        ":load_balancer_interface",
        ":thread_local_cluster_interface",
        ":upstream_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/common:random_generator_interface",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_factory_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/thread_local:thread_local_interface",
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
        "//include/envoy/network:address_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/stats:primitive_stats_macros",
        "//include/envoy/stats:stats_macros",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "load_balancer_interface",
    hdrs = ["load_balancer.h"],
    deps = [
        ":upstream_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/upstream:types_interface",
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
        "//include/envoy/common:time_interface",
        "@envoy_api//envoy/data/cluster/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "retry_interface",
    hdrs = ["retry.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/upstream:types_interface",
        "//include/envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "resource_manager_interface",
    hdrs = ["resource_manager.h"],
    deps = ["//include/envoy/common:resource_interface"],
)

envoy_cc_library(
    name = "thread_local_cluster_interface",
    hdrs = ["thread_local_cluster.h"],
    deps = [
        ":load_balancer_interface",
        ":upstream_interface",
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
        "//include/envoy/common:callback",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/config:typed_metadata_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/ssl:context_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/upstream:types_interface",
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
        "//include/envoy/common:callback",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/config:typed_metadata_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/ssl:context_interface",
        "//include/envoy/ssl:context_manager_interface",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)
