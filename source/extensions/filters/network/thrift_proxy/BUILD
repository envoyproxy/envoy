load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception_impl.cc"],
    hdrs = ["app_exception_impl.h"],
    deps = [
        ":protocol_interface",
        ":thrift_lib",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "buffer_helper_lib",
    srcs = ["buffer_helper.cc"],
    hdrs = ["buffer_helper.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
        "//source/common/common:safe_memcpy_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":app_exception_lib",
        ":auto_protocol_lib",
        ":auto_transport_lib",
        ":binary_protocol_lib",
        ":compact_protocol_lib",
        ":conn_manager_lib",
        ":decoder_lib",
        ":framed_transport_lib",
        ":header_transport_lib",
        ":protocol_interface",
        ":protocol_options_config_lib",
        ":twitter_protocol_lib",
        ":unframed_transport_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/registry",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy/router:rds_lib",
        "//source/extensions/filters/network/thrift_proxy/router:router_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = ["conn_manager.cc"],
    hdrs = ["conn_manager.h"],
    external_deps = ["abseil_any"],
    deps = [
        ":app_exception_lib",
        ":decoder_lib",
        ":filter_utils_lib",
        ":protocol_converter_lib",
        ":protocol_interface",
        ":stats_lib",
        ":transport_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/network:filter_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "decoder_events_lib",
    hdrs = ["decoder_events.h"],
    deps = [
        ":metadata_lib",
        ":thrift_lib",
    ],
)

envoy_cc_library(
    name = "passthrough_decoder_event_handler_lib",
    hdrs = ["passthrough_decoder_event_handler.h"],
    deps = [
        ":decoder_events_lib",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":app_exception_lib",
        ":protocol_interface",
        ":stats_lib",
        ":transport_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "filter_utils_lib",
    srcs = ["filter_utils.cc"],
    hdrs = ["filter_utils.h"],
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":thrift_lib",
        ":tracing_interface",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "tracing_interface",
    hdrs = ["tracing.h"],
    external_deps = ["abseil_optional"],
    deps = [],
)

envoy_cc_library(
    name = "protocol_converter_lib",
    hdrs = [
        "protocol_converter.h",
    ],
    deps = [
        ":decoder_events_lib",
        ":protocol_interface",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = [
        "protocol.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":conn_state_lib",
        ":decoder_events_lib",
        ":metadata_lib",
        ":thrift_lib",
        ":thrift_object_interface",
        ":transport_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "auto_protocol_lib",
    srcs = [
        "auto_protocol_impl.cc",
    ],
    hdrs = [
        "auto_protocol_impl.h",
    ],
    deps = [
        ":binary_protocol_lib",
        ":buffer_helper_lib",
        ":compact_protocol_lib",
        ":protocol_interface",
        ":twitter_protocol_lib",
        "//source/common/common:macros",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "binary_protocol_lib",
    srcs = [
        "binary_protocol_impl.cc",
    ],
    hdrs = [
        "binary_protocol_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":protocol_interface",
        "//source/common/common:macros",
        "//source/common/runtime:runtime_features_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "compact_protocol_lib",
    srcs = [
        "compact_protocol_impl.cc",
    ],
    hdrs = [
        "compact_protocol_impl.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        ":protocol_interface",
        "//source/common/common:macros",
        "//source/common/runtime:runtime_features_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "twitter_protocol_lib",
    srcs = [
        "twitter_protocol_impl.cc",
    ],
    hdrs = [
        "twitter_protocol_impl.h",
    ],
    deps = [
        ":binary_protocol_lib",
        ":buffer_helper_lib",
        ":protocol_interface",
        ":thrift_object_lib",
        "//source/common/common:macros",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "transport_interface",
    hdrs = ["transport.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        ":metadata_lib",
        ":thrift_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "conn_state_lib",
    hdrs = ["conn_state.h"],
    deps = [
        "//envoy/tcp:conn_pool_interface",
    ],
)

envoy_cc_library(
    name = "protocol_options_config_lib",
    hdrs = ["protocol_options_config.h"],
    deps = [
        ":thrift_lib",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "thrift_lib",
    hdrs = ["thrift.h"],
    deps = [
        "//envoy/tcp:conn_pool_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "thrift_object_interface",
    hdrs = ["thrift_object.h"],
    deps = [
        ":thrift_lib",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "thrift_object_lib",
    srcs = ["thrift_object_impl.cc"],
    hdrs = ["thrift_object_impl.h"],
    deps = [
        ":decoder_lib",
        ":thrift_lib",
        ":thrift_object_interface",
        ":unframed_transport_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "auto_transport_lib",
    srcs = [
        "auto_transport_impl.cc",
    ],
    hdrs = [
        "auto_transport_impl.h",
    ],
    deps = [
        ":binary_protocol_lib",
        ":buffer_helper_lib",
        ":compact_protocol_lib",
        ":framed_transport_lib",
        ":header_transport_lib",
        ":transport_interface",
        ":twitter_protocol_lib",
        ":unframed_transport_lib",
        "//source/common/common:assert_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "framed_transport_lib",
    srcs = [
        "framed_transport_impl.cc",
    ],
    hdrs = [
        "framed_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "header_transport_lib",
    srcs = [
        "header_transport_impl.cc",
    ],
    hdrs = [
        "header_transport_impl.h",
    ],
    deps = [
        ":app_exception_lib",
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "unframed_transport_lib",
    srcs = [
        "unframed_transport_impl.cc",
    ],
    hdrs = [
        "unframed_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
    alwayslink = 1,
)
