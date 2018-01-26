licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats_impl.cc"],
    hdrs = ["stats_impl.h"],
    external_deps = [
        "abseil_strings",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_library(
    name = "statsd_lib",
    srcs = ["statsd.cc"],
    hdrs = ["statsd.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
    ],
)

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
    name = "thread_local_store_lib",
    srcs = ["thread_local_store.cc"],
    hdrs = ["thread_local_store.h"],
    deps = [
        ":stats_lib",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)
