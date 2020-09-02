licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "conn_info_lib",
    srcs = select({
        "//bazel:linux": ["conn_info_linux.cc"],
        "//conditions:default": ["conn_info_default.cc"],
    }),
    hdrs = ["conn_info.h"],
    deps = ["//include/envoy/network:socket_interface"],
)

envoy_cc_library(
    name = "conn_info_linux",
    hdrs = ["conn_info_linux.h"],
)

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
