load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "constant",
    hdrs = ["constant.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_library(
    name = "stats_interface",
    hdrs = ["stats.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
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
        ":constant",
        ":protocol_interface",
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
        "//envoy/network:filter_interface",
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
        ":constant",
        ":protocol_lib",
        ":rocketmq_lib",
        ":stats_interface",
        "//contrib/rocketmq_proxy/filters/network/source/router:router_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/tcp:conn_pool_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:linked_object",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//contrib/envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":conn_manager_lib",
        "//contrib/rocketmq_proxy/filters/network/source/router:route_matcher",
        "//contrib/rocketmq_proxy/filters/network/source/router:router_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    deps = [
        "//source/common/http:header_map_lib",
        "@com_google_absl//absl/types:optional",
    ],
)
