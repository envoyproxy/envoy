licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "cluster_manager_interface",
    hdrs = ["cluster_manager.h"],
    external_deps = [
        "envoy_bootstrap",
        "envoy_cds",
    ],
    deps = [
        ":load_balancer_interface",
        ":thread_local_cluster_interface",
        ":upstream_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
    ],
)

envoy_cc_library(
    name = "health_checker_interface",
    hdrs = ["health_checker.h"],
    deps = [":upstream_interface"],
)

envoy_cc_library(
    name = "health_check_host_monitor_interface",
    hdrs = ["health_check_host_monitor.h"],
)

envoy_cc_library(
    name = "host_description_interface",
    hdrs = ["host_description.h"],
    external_deps = ["envoy_base"],
    deps = [
        ":health_check_host_monitor_interface",
        ":outlier_detection_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "load_balancer_interface",
    hdrs = ["load_balancer.h"],
    deps = [
        ":upstream_interface",
        "//include/envoy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "load_balancer_type_interface",
    hdrs = ["load_balancer_type.h"],
    external_deps = ["envoy_cds"],
    deps = [
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "outlier_detection_interface",
    hdrs = ["outlier_detection.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "resource_manager_interface",
    hdrs = ["resource_manager.h"],
)

envoy_cc_library(
    name = "thread_local_cluster_interface",
    hdrs = ["thread_local_cluster.h"],
)

envoy_cc_library(
    name = "upstream_interface",
    hdrs = ["upstream.h"],
    deps = [
        ":health_check_host_monitor_interface",
        ":load_balancer_type_interface",
        ":resource_manager_interface",
        "//include/envoy/common:callback",
        "//include/envoy/common:optional",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/ssl:context_interface",
    ],
)
