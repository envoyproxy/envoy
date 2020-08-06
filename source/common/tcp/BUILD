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
        "original_conn_pool.cc",
    ],
    hdrs = [
        "conn_pool.h",
        "original_conn_pool.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)
