load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "buffer_helper_lib",
    srcs = ["buffer_helper.cc"],
    hdrs = ["buffer_helper.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_cc_library(
    name = "hessian_utils_lib",
    srcs = ["hessian_utils.cc"],
    hdrs = ["hessian_utils.h"],
    deps = [
        ":buffer_helper_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = ["protocol.h"],
    deps = [
        ":buffer_helper_lib",
        ":message_lib",
        ":metadata_lib",
        ":serializer_interface",
        "//include/envoy/config:typed_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "dubbo_protocol_impl_lib",
    srcs = ["dubbo_protocol_impl.cc"],
    hdrs = ["dubbo_protocol_impl.h"],
    deps = [
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "serializer_interface",
    srcs = ["serializer_impl.cc"],
    hdrs = [
        "protocol_constants.h",
        "serializer.h",
        "serializer_impl.h",
    ],
    deps = [
        ":message_lib",
        ":metadata_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/config:typed_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "dubbo_hessian2_serializer_impl_lib",
    srcs = ["dubbo_hessian2_serializer_impl.cc"],
    hdrs = [
        "dubbo_hessian2_serializer_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":hessian_utils_lib",
        ":serializer_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":decoder_events_lib",
        ":dubbo_hessian2_serializer_impl_lib",
        ":dubbo_protocol_impl_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":conn_manager_lib",
        "//include/envoy/registry",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:well_known_names",
        "//source/extensions/filters/network/dubbo_proxy/router:config",
        "//source/extensions/filters/network/dubbo_proxy/router:route_matcher",
        "//source/extensions/filters/network/dubbo_proxy/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":message_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "message_lib",
    hdrs = [
        "message.h",
        "message_impl.h",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "decoder_events_lib",
    hdrs = ["decoder_event_handler.h"],
    deps = [
        ":metadata_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception.cc"],
    hdrs = ["app_exception.h"],
    deps = [
        ":message_lib",
        ":metadata_lib",
        ":protocol_interface",
        ":serializer_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "heartbeat_response_lib",
    srcs = ["heartbeat_response.cc"],
    hdrs = ["heartbeat_response.h"],
    deps = [
        ":metadata_lib",
        ":protocol_interface",
        ":serializer_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
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
        ":app_exception_lib",
        ":decoder_events_lib",
        ":decoder_lib",
        ":dubbo_hessian2_serializer_impl_lib",
        ":dubbo_protocol_impl_lib",
        ":heartbeat_response_lib",
        ":stats_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
        "//source/extensions/filters/network/dubbo_proxy/router:router_interface",
        "@envoy_api//envoy/extensions/filters/network/dubbo_proxy/v3:pkg_cc_proto",
    ],
)
