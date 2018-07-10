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
    deps = [
        ":health_checker_interface",
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
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:admin_interface",
        "@envoy_api//envoy/api/v2:cds_cc",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_library(
    name = "health_checker_interface",
    hdrs = ["health_checker.h"],
    deps = [
        ":upstream_interface",
        "@envoy_api//envoy/data/core/v2alpha:health_check_event_cc",
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
        "//include/envoy/stats:stats_macros",
        "@envoy_api//envoy/api/v2/core:base_cc",
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
    deps = [
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:cds_cc",
    ],
)

envoy_cc_library(
    name = "outlier_detection_interface",
    hdrs = ["outlier_detection.h"],
    external_deps = ["abseil_optional"],
    deps = [
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
    external_deps = ["abseil_optional"],
    deps = [
        ":health_check_host_monitor_interface",
        ":load_balancer_type_interface",
        ":resource_manager_interface",
        "//include/envoy/common:callback",
        "//include/envoy/http:codec_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/ssl:context_interface",
    ],
)
