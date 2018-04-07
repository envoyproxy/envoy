licenses(["notice"])  # Apache 2
# Stats sink for the gRPC metrics service: https://github.com/envoyproxy/data-plane-api/blob/master/envoy/service/metrics/v2/metrics_service.proto

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "metrics_service_grpc_lib",
    srcs = ["grpc_metrics_service_impl.cc"],
    hdrs = ["grpc_metrics_service_impl.h"],
    deps = [
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/service/metrics/v2:metrics_service_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
        "@envoy_api//envoy/service/metrics/v2:metrics_service_cc",
    ],
)
