load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
    ],
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
        "//envoy/upstream:cluster_manager_interface",
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
        "//envoy/router:router_interface",
        "//envoy/stats:timespan_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":codec_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "redis_command_stats_lib",
    srcs = ["redis_command_stats.cc"],
    hdrs = ["redis_command_stats.h"],
    deps = [
        ":codec_interface",
        ":supported_commands_lib",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "fault_interface",
    hdrs = ["fault.h"],
    deps = [
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "fault_lib",
    srcs = ["fault_impl.cc"],
    hdrs = ["fault_impl.h"],
    deps = [
        ":codec_lib",
        ":fault_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)
