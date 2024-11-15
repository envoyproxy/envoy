load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = [
        "conn_pool.cc",
    ],
    hdrs = [
        "conn_pool.h",
    ],
    deps = [
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "async_tcp_client_lib",
    srcs = [
        "async_tcp_client_impl.cc",
    ],
    hdrs = [
        "async_tcp_client_impl.h",
    ],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/stats:timespan_interface",
        "//envoy/tcp:async_tcp_client_interface",
        "//envoy/upstream:thread_local_cluster_interface",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
    ],
)
