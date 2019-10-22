licenses(["notice"])  # Apache 2

# Redis proxy L4 network filter. Implements consistent hashing and observability for large redis
# clusters.
# Public docs: docs/root/configuration/network_filters/redis_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "command_splitter_interface",
    hdrs = ["command_splitter.h"],
    deps = [
        "//source/extensions/filters/network/common/redis:codec_interface",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = ["config.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        ":conn_pool_interface",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "command_splitter_lib",
    srcs = ["command_splitter_impl.cc"],
    hdrs = ["command_splitter_impl.h"],
    deps = [
        ":command_splitter_interface",
        ":router_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:to_lower_table_lib",
        "//source/common/common:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/common/redis:supported_commands_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
        ":config_interface",
        ":conn_pool_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:address_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters/redis:redis_cluster_lb",
        "//source/extensions/common/redis:redirection_mgr_interface",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
        ":command_splitter_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/common/redis:codec_interface",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/config:filter_json_lib",
        "//source/extensions/common/redis:redirection_mgr_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/redis:redis_command_stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//source/extensions/filters/network/redis_proxy:proxy_filter_lib",
        "//source/extensions/filters/network/redis_proxy:router_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:to_lower_table_lib",
        "//source/extensions/filters/network/common/redis:supported_commands_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)
