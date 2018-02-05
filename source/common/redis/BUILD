licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        "//include/envoy/redis:codec_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "command_splitter_lib",
    srcs = ["command_splitter_impl.cc"],
    hdrs = ["command_splitter_impl.h"],
    deps = [
        ":supported_commands_lib",
        "//include/envoy/redis:command_splitter_interface",
        "//include/envoy/redis:conn_pool_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:to_lower_table_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
        ":codec_lib",
        "//include/envoy/redis:conn_pool_interface",
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
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/redis:codec_interface",
        "//include/envoy/redis:command_splitter_interface",
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
