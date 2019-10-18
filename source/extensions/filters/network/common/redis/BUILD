licenses(["notice"])  # Apache 2

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
    name = "utility_interface",
    hdrs = ["utility.h"],
    deps = [":codec_interface"],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":codec_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:stack_array",
        "//source/common/common:utility_lib",
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
    name = "client_interface",
    hdrs = ["client.h"],
    deps = [
        ":codec_lib",
        ":redis_command_stats_lib",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":client_interface",
        ":codec_lib",
        ":utility_lib",
        "//include/envoy/router:router_interface",
        "//include/envoy/stats:timespan_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":codec_lib",
    ],
)

envoy_cc_library(
    name = "redis_command_stats_lib",
    srcs = ["redis_command_stats.cc"],
    hdrs = ["redis_command_stats.h"],
    deps = [
        ":codec_interface",
        ":supported_commands_lib",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
        "//source/common/common:to_lower_table_lib",
        "//source/common/common:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:timespan_lib",
    ],
)
