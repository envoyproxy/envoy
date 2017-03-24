package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "cluster_manager_includes",
    hdrs = ["cluster_manager.h"],
    deps = [
        ":load_balancer_includes",
        ":thread_local_cluster_includes",
        ":upstream_includes",
        "//include/envoy/http:async_client_includes",
        "//include/envoy/http:conn_pool_includes",
        "//include/envoy/json:json_object_includes",
    ],
)

envoy_cc_library(
    name = "health_checker_includes",
    hdrs = ["health_checker.h"],
    deps = [":upstream_includes"],
)

envoy_cc_library(
    name = "host_description_includes",
    hdrs = ["host_description.h"],
    deps = [
        ":outlier_detection_includes",
        "//include/envoy/network:address_includes",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "load_balancer_includes",
    hdrs = ["load_balancer.h"],
    deps = [":upstream_includes"],
)

envoy_cc_library(
    name = "load_balancer_type_includes",
    hdrs = ["load_balancer_type.h"],
)

envoy_cc_library(
    name = "outlier_detection_includes",
    hdrs = ["outlier_detection.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/common:time_includes",
    ],
)

envoy_cc_library(
    name = "resource_manager_includes",
    hdrs = ["resource_manager.h"],
)

envoy_cc_library(
    name = "thread_local_cluster_includes",
    hdrs = ["thread_local_cluster.h"],
)

envoy_cc_library(
    name = "upstream_includes",
    hdrs = ["upstream.h"],
    deps = [
        ":load_balancer_type_includes",
        ":resource_manager_includes",
        "//include/envoy/common:optional_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/ssl:context_includes",
    ],
)
