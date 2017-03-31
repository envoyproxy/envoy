package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "echo_lib",
    srcs = ["echo.cc"],
    hdrs = ["echo.h"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:filter_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:filter_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:empty_string",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "tcp_proxy_lib",
    srcs = ["tcp_proxy.cc"],
    hdrs = ["tcp_proxy.h"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:filter_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/upstream:cluster_manager_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:utility_lib",
    ],
)
