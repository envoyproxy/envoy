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
        "//include/envoy/event:deferred_deletable_no_include_prefix",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/event:timer_interface_no_include_prefix",
        "//include/envoy/network:connection_interface_no_include_prefix",
        "//include/envoy/stats:stats_interface_no_include_prefix",
        "//include/envoy/stats:timespan_interface_no_include_prefix",
        "//include/envoy/tcp:conn_pool_interface_no_include_prefix",
        "//include/envoy/upstream:upstream_interface_no_include_prefix",
        "//source/common/common:linked_object",
        "//source/common/common:utility_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:upstream_lib",
    ],
)
