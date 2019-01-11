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
        "//include/envoy/common:time_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan",
        "//include/envoy/stream_info:filter_state_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:macros",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:upstream_server_name_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/upstream:load_balancer_lib",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:tcp_proxy_cc",
    ],
)
