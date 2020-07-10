load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_library(
    name = "stats_interface",
    hdrs = ["stats.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "rocketmq_interface",
    hdrs = [
        "topic_route.h",
    ],
    deps = [
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "rocketmq_lib",
    srcs = [
        "topic_route.cc",
    ],
    deps = [
        ":rocketmq_interface",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = ["protocol.h"],
    deps = [
        ":metadata_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "protocol_lib",
    srcs = ["protocol.cc"],
    deps = [
        ":protocol_interface",
        ":well_known_names",
        "//source/common/common:enum_to_int",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = [
        "codec.cc",
    ],
    hdrs = [
        "codec.h",
    ],
    deps = [
        ":protocol_lib",
        "//include/envoy/network:filter_interface",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = [
        "active_message.cc",
        "conn_manager.cc",
    ],
    hdrs = [
        "active_message.h",
        "conn_manager.h",
    ],
    deps = [
        ":codec_lib",
        ":protocol_lib",
        ":rocketmq_lib",
        ":stats_interface",
        ":well_known_names",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/rocketmq_proxy/router:router_interface",
        "@envoy_api//envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":conn_manager_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/rocketmq_proxy/router:route_matcher",
        "//source/extensions/filters/network/rocketmq_proxy/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/http:header_map_lib",
    ],
)
