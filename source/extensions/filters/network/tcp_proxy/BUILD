licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tcp_proxy",
    srcs = ["tcp_proxy.cc"],
    hdrs = ["tcp_proxy.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/config:well_known_names",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:utility_lib",
        "//source/common/request_info:request_info_lib",
        "//source/common/router:config_lib",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:tcp_proxy_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tcp_proxy",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
    ],
)
