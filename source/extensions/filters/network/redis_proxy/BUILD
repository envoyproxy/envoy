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
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = ["//include/envoy/buffer:buffer_interface"],
)

envoy_cc_library(
    name = "command_splitter_interface",
    hdrs = ["command_splitter.h"],
    deps = [":codec_interface"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":codec_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "command_splitter_lib",
    srcs = ["command_splitter_impl.cc"],
    hdrs = ["command_splitter_impl.h"],
    deps = [
        ":command_splitter_interface",
        ":conn_pool_interface",
        ":supported_commands_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:to_lower_table_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
        ":codec_lib",
        ":conn_pool_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
    ],
)

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
        ":codec_interface",
        ":command_splitter_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
    ],
)

envoy_cc_library(
    name = "supported_commands_lib",
    hdrs = ["supported_commands.h"],
    deps = [
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/redis_proxy:codec_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//source/extensions/filters/network/redis_proxy:proxy_filter_lib",
    ],
)
